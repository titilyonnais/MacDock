#include "glass_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "../core/log.h"
#include "../geom/smooth_rect.h"

#include "blur_ps.h"
#include "downsample_ps.h"
#include "fullscreen_vs.h"
#include "glass_ps.h"

namespace md {

namespace {
constexpr int kLutSize = 128;
constexpr double kLutLo = -2, kLutHi = 2;
constexpr UINT kDownsample = 4;

struct QuadCb { float rect[4]; float target[2]; float pad[2]; };
struct DownCb { float srcSize[2]; float scRgb; float sdrWhite; };
struct BlurCb { float dir[2]; float texSize[2]; float sigma; float radius; float pad[2]; };
struct GlassCb {
    float shapeRect[4];
    float radius, strength, shadowOpacity, bevel;
    float refraction, chromatic, fresnel, specular;
    float tint, saturation, shadowBlur, shadowOffset;
    float scale, dark, targetSize[2];
    float maxMip, opacity, pad[2];
};
static_assert(sizeof(GlassCb) % 16 == 0);
} // namespace

template <class T> void GlassRenderer::setConstants(ID3D11DeviceContext* ctx, ID3D11Buffer* buf, const T& data) {
    D3D11_MAPPED_SUBRESOURCE m{};
    if (SUCCEEDED(ctx->Map(buf, 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, &data, sizeof data);
        ctx->Unmap(buf, 0);
    }
}

bool GlassRenderer::init(ID3D11Device* dev) {
    dev_ = dev;
    down_ = blurA_ = blurB_ = {};
    if (!dev_) return false;
    if (FAILED(dev_->CreateVertexShader(g_fullscreen_vs, sizeof g_fullscreen_vs, nullptr, &vs_)) ||
        FAILED(dev_->CreatePixelShader(g_downsample_ps, sizeof g_downsample_ps, nullptr, &downPs_)) ||
        FAILED(dev_->CreatePixelShader(g_blur_ps, sizeof g_blur_ps, nullptr, &blurPs_)) ||
        FAILED(dev_->CreatePixelShader(g_glass_ps, sizeof g_glass_ps, nullptr, &glassPs_))) {
        log::error(L"Création des shaders du verre impossible");
        return false;
    }
    auto makeCb = [&](UINT size, Com<ID3D11Buffer>& out) {
        D3D11_BUFFER_DESC d{};
        d.ByteWidth = (size + 15) & ~15u;
        d.Usage = D3D11_USAGE_DYNAMIC;
        d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        return SUCCEEDED(dev_->CreateBuffer(&d, nullptr, &out));
    };
    if (!makeCb(sizeof(QuadCb), quadCb_) || !makeCb(sizeof(DownCb), downCb_) || !makeCb(sizeof(BlurCb), blurCb_) ||
        !makeCb(sizeof(GlassCb), glassCb_))
        return false;

    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(dev_->CreateSamplerState(&s, &linear_))) return false;

    D3D11_BLEND_DESC b{};
    auto& rt = b.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
    rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev_->CreateBlendState(&b, &premulBlend_))) return false;
    rt.BlendEnable = FALSE;
    if (FAILED(dev_->CreateBlendState(&b, &noBlend_))) return false;

    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    if (FAILED(dev_->CreateRasterizerState(&r, &raster_))) return false;

    // Champ de distance d'un coin continu (rayon 1), lu par le shader comme sampleCornerField.
    std::vector<float> lut = cornerDistanceField(kLutSize, kLutLo, kLutHi);
    D3D11_TEXTURE2D_DESC t{};
    t.Width = t.Height = kLutSize;
    t.MipLevels = t.ArraySize = 1;
    t.Format = DXGI_FORMAT_R32_FLOAT;
    t.SampleDesc.Count = 1;
    t.Usage = D3D11_USAGE_IMMUTABLE;
    t.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{lut.data(), UINT(kLutSize * sizeof(float)), 0};
    Com<ID3D11Texture2D> lutTex;
    return SUCCEEDED(dev_->CreateTexture2D(&t, &init, &lutTex)) &&
           SUCCEEDED(dev_->CreateShaderResourceView(lutTex.Get(), nullptr, &lutSrv_));
}

bool GlassRenderer::makeTarget(Target& t, UINT w, UINT h, bool mips) {
    t = {};
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = mips ? 0 : 1;
    d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.MiscFlags = mips ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;
    if (FAILED(dev_->CreateTexture2D(&d, nullptr, &t.tex))) return false;
    D3D11_RENDER_TARGET_VIEW_DESC rv{};
    rv.Format = d.Format;
    rv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
    if (FAILED(dev_->CreateRenderTargetView(t.tex.Get(), &rv, &t.rtv)) ||
        FAILED(dev_->CreateShaderResourceView(t.tex.Get(), nullptr, &t.srv)))
        return false;
    t.tex->GetDesc(&d);
    t.w = w;
    t.h = h;
    t.mips = d.MipLevels;
    return true;
}

bool GlassRenderer::ensureTargets(UINT w, UINT h) {
    UINT dw = std::max(1u, (w + kDownsample - 1) / kDownsample), dh = std::max(1u, (h + kDownsample - 1) / kDownsample);
    if (down_.tex && down_.w == dw && down_.h == dh) return true;
    return makeTarget(down_, dw, dh, false) && makeTarget(blurA_, dw, dh, false) && makeTarget(blurB_, dw, dh, true);
}

void GlassRenderer::drawQuad(ID3D11DeviceContext* ctx, float l, float t, float r, float b, UINT tw, UINT th) {
    QuadCb q{{l, t, r, b}, {float(tw), float(th)}, {}};
    setConstants(ctx, quadCb_.Get(), q);
    ctx->Draw(4, 0);
}

bool GlassRenderer::render(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* backdrop, UINT w, UINT h,
                           ID3D11RenderTargetView* target, std::span<const GlassShape> shapes, const GlassParams& p) {
    if (!ctx || !backdrop || !target || !w || !h || !glassPs_ || !ensureTargets(w, h)) return false;

    ctx->ClearState();
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, quadCb_.GetAddressOf());
    ctx->RSSetState(raster_.Get());
    ctx->PSSetSamplers(0, 1, linear_.GetAddressOf());
    ID3D11ShaderResourceView* none[2] = {};
    auto viewport = [&](UINT vw, UINT vh) {
        D3D11_VIEWPORT v{0, 0, float(vw), float(vh), 0, 1};
        ctx->RSSetViewports(1, &v);
    };

    // 1. Réduction (et conversion HDR → sRGB).
    ctx->OMSetBlendState(noBlend_.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetRenderTargets(1, down_.rtv.GetAddressOf(), nullptr);
    viewport(down_.w, down_.h);
    setConstants(ctx, downCb_.Get(), DownCb{{float(w), float(h)}, p.backdropIsScRgb ? 1.0f : 0.0f, p.sdrWhiteScale});
    ctx->PSSetConstantBuffers(1, 1, downCb_.GetAddressOf());
    ctx->PSSetShader(downPs_.Get(), nullptr, 0);
    ctx->PSSetShaderResources(0, 1, &backdrop);
    drawQuad(ctx, 0, 0, float(down_.w), float(down_.h), down_.w, down_.h);

    // 2. Flou séparable : horizontal (réduit → A) puis vertical (A → B), puis mipmaps de B (luminance moyenne).
    float sigma = std::max(0.25f, p.blurSigmaPx / float(kDownsample));
    float radius = std::min(30.0f, std::ceil(3 * sigma));
    ctx->PSSetShader(blurPs_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, blurCb_.GetAddressOf());
    ctx->PSSetShaderResources(0, 1, none);
    ctx->OMSetRenderTargets(1, blurA_.rtv.GetAddressOf(), nullptr);
    setConstants(ctx, blurCb_.Get(), BlurCb{{1, 0}, {float(down_.w), float(down_.h)}, sigma, radius, {}});
    ctx->PSSetShaderResources(0, 1, down_.srv.GetAddressOf());
    drawQuad(ctx, 0, 0, float(down_.w), float(down_.h), down_.w, down_.h);
    ctx->PSSetShaderResources(0, 1, none);
    ctx->OMSetRenderTargets(1, blurB_.rtv.GetAddressOf(), nullptr);
    setConstants(ctx, blurCb_.Get(), BlurCb{{0, 1}, {float(down_.w), float(down_.h)}, sigma, radius, {}});
    ctx->PSSetShaderResources(0, 1, blurA_.srv.GetAddressOf());
    drawQuad(ctx, 0, 0, float(down_.w), float(down_.h), down_.w, down_.h);
    ctx->PSSetShaderResources(0, 1, none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    ctx->GenerateMips(blurB_.srv.Get());

    // 3. Formes de verre et ombres, mélangées en alpha prémultiplié sur la cible vidée.
    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(target, clear);
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->OMSetBlendState(premulBlend_.Get(), nullptr, 0xFFFFFFFF);
    viewport(w, h);
    ctx->PSSetShader(glassPs_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(1, 1, glassCb_.GetAddressOf());
    ID3D11ShaderResourceView* srvs[2] = {blurB_.srv.Get(), lutSrv_.Get()};
    ctx->PSSetShaderResources(0, 2, srvs);
    for (const GlassShape& s : shapes) {
        if (!(s.right > s.left) || !(s.bottom > s.top) || s.opacity <= 0) continue;
        GlassCb c{};
        c.shapeRect[0] = s.left;
        c.shapeRect[1] = s.top;
        c.shapeRect[2] = s.right;
        c.shapeRect[3] = s.bottom;
        c.radius = s.radius;
        c.strength = s.strength;
        c.shadowOpacity = s.shadowOpacity;
        c.bevel = p.bevelPx;
        c.refraction = p.refraction;
        c.chromatic = p.chromatic;
        c.fresnel = p.fresnel;
        c.specular = p.specular;
        c.tint = p.tint;
        c.saturation = p.saturation;
        c.shadowBlur = p.shadowBlurPx;
        c.shadowOffset = p.shadowOffsetPx;
        c.scale = p.scale;
        c.dark = p.dark ? 1.0f : 0.0f;
        // Étendue couverte par la texture réduite (4·dw × 4·dh), qui dépasse la cible si w ou h n'est pas un multiple de 4.
        c.targetSize[0] = float(down_.w * kDownsample);
        c.targetSize[1] = float(down_.h * kDownsample);
        c.maxMip = float(blurB_.mips - 1);
        c.opacity = s.opacity;
        setConstants(ctx, glassCb_.Get(), c);
        float margin = s.shadowOpacity > 0 ? 3 * p.shadowBlurPx + p.shadowOffsetPx : 2;
        drawQuad(ctx, std::max(0.0f, s.left - margin), std::max(0.0f, s.top - margin),
                 std::min(float(w), s.right + margin), std::min(float(h), s.bottom + margin), w, h);
    }
    ctx->PSSetShaderResources(0, 2, none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

} // namespace md
