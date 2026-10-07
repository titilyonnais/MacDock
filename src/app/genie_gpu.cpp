#include "genie_gpu.h"

#include <cfloat>
#include <cmath>
#include <cstring>

#include "../core/log.h"

#include "genie_ps.h"
#include "genie_vs.h"

namespace md {

namespace {
struct ParamsCb {
    float size[2];
    float toSdr, white;
};
struct Vertex {
    float x, y, u, v;
};
constexpr float kEdge = 1;   // débord du maillage pour l'anticrénelage (pixels)

// p prolongé de e pixels dans le sens q → p, coordonnées de texture extrapolées ; inchangé si p et q sont confondus.
GenieVertex extend(const GenieVertex& p, const GenieVertex& q, float e) {
    const float dx = p.x - q.x, dy = p.y - q.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return p;
    const float k = e / len;
    return {p.x + dx * k, p.y + dy * k, p.u + (p.u - q.u) * k, p.v + (p.v - q.v) * k};
}
} // namespace

bool GenieGpu::init(ID3D11Device* dev) {
    *this = GenieGpu{};
    dev_ = dev;
    if (!dev_) return false;
    const D3D11_INPUT_ELEMENT_DESC in[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    if (FAILED(dev_->CreateVertexShader(g_genie_vs, sizeof g_genie_vs, nullptr, &vs_)) ||
        FAILED(dev_->CreatePixelShader(g_genie_ps, sizeof g_genie_ps, nullptr, &ps_)) ||
        FAILED(dev_->CreateInputLayout(in, 2, g_genie_vs, sizeof g_genie_vs, &layout_))) {
        log::error(L"Génie : shaders impossibles");
        return false;
    }
    D3D11_BUFFER_DESC cb{};
    cb.ByteWidth = sizeof(ParamsCb);
    cb.Usage = D3D11_USAGE_DYNAMIC;
    cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cb.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (FAILED(dev_->CreateBuffer(&cb, nullptr, &cb_))) return false;

    D3D11_SAMPLER_DESC s{};
    s.Filter = D3D11_FILTER_ANISOTROPIC;   // les bandes resserrées sont écrasées dans un seul sens
    s.MaxAnisotropy = 8;
    s.AddressU = s.AddressV = s.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    s.MaxLOD = FLT_MAX;
    if (FAILED(dev_->CreateSamplerState(&s, &sampler_))) return false;

    D3D11_BLEND_DESC b{};
    auto& rt = b.RenderTarget[0];
    rt.BlendEnable = TRUE;
    rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;   // prémultiplié
    rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(dev_->CreateBlendState(&b, &blend_))) return false;

    D3D11_RASTERIZER_DESC r{};
    r.FillMode = D3D11_FILL_SOLID;
    r.CullMode = D3D11_CULL_NONE;
    r.DepthClipEnable = TRUE;
    return SUCCEEDED(dev_->CreateRasterizerState(&r, &raster_));
}

void GenieGpu::dropSource() {
    src_.Reset();
    srv_.Reset();
    scRgb_ = false;
}

bool GenieGpu::setSource(ID3D11DeviceContext* ctx, ID3D11Texture2D* frame, UINT w, UINT h) {
    // Image refusée : la précédente reste (une capture d'avance en a peut-être déjà livré une bonne).
    if (!dev_ || !ctx || !frame || !w || !h) return false;
    D3D11_TEXTURE2D_DESC fd{};
    frame->GetDesc(&fd);
    if (fd.Width < w || fd.Height < h) return false;
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = 0;   // chaîne complète : nette jusque dans la case du Dock
    d.ArraySize = 1;
    d.Format = fd.Format;   // BGRA8 (SDR) ou RGBA16F (scRGB)
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    d.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    Com<ID3D11Texture2D> src;
    Com<ID3D11ShaderResourceView> srv;
    if (FAILED(dev_->CreateTexture2D(&d, nullptr, &src)) || FAILED(dev_->CreateShaderResourceView(src.Get(), nullptr, &srv)))
        return false;
    src_ = std::move(src);
    srv_ = std::move(srv);
    const D3D11_BOX box{0, 0, 0, w, h, 1};
    ctx->CopySubresourceRegion(src_.Get(), 0, 0, 0, 0, frame, 0, &box);
    scRgb_ = fd.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
    ctx->GenerateMips(srv_.Get());
    return true;
}

bool GenieGpu::draw(ID3D11DeviceContext* ctx, ID3D11Texture2D* dst, UINT w, UINT h, POINT origin,
                    const std::vector<GenieVertex>& mesh) {
    if (!dev_ || !ctx || !dst) return false;
    D3D11_TEXTURE2D_DESC dd{};
    dst->GetDesc(&dd);
    Com<ID3D11RenderTargetView> rtv;
    if (!w || !h || dd.Width < w || dd.Height < h || FAILED(dev_->CreateRenderTargetView(dst, nullptr, &rtv))) return false;
    const float clear[4] = {0, 0, 0, 0};
    ctx->ClearRenderTargetView(rtv.Get(), clear);
    const std::size_t lines = mesh.size() / 2;
    if (!srv_ || lines < 2) return true;
    // Lignes du maillage prolongées d'un pixel de chaque côté, plus une ligne avant la première et après la dernière :
    // la rampe d'anticrénelage du shader tombe hors de la fenêtre.
    std::vector<GenieVertex> grid;
    grid.reserve((lines + 2) * 2);
    const auto widen = [&](const GenieVertex& l, const GenieVertex& r) {
        grid.push_back(extend(l, r, kEdge));
        grid.push_back(extend(r, l, kEdge));
    };
    widen(extend(mesh[0], mesh[2], kEdge), extend(mesh[1], mesh[3], kEdge));
    for (std::size_t i = 0; i < lines; ++i) widen(mesh[2 * i], mesh[2 * i + 1]);
    const std::size_t last = 2 * (lines - 1);
    widen(extend(mesh[last], mesh[last - 2], kEdge), extend(mesh[last + 1], mesh[last - 1], kEdge));

    std::vector<Vertex> tri;   // deux triangles par rangée, en pixels de la cible
    tri.reserve((grid.size() / 2 - 1) * 6);
    const auto put = [&](const GenieVertex& p) { tri.push_back({p.x - float(origin.x), p.y - float(origin.y), p.u, p.v}); };
    for (std::size_t k = 0; k + 3 < grid.size(); k += 2) {
        const GenieVertex &a = grid[k], &b = grid[k + 1], &c = grid[k + 2], &d = grid[k + 3];
        put(a), put(b), put(c), put(b), put(d), put(c);
    }
    const UINT bytes = UINT(tri.size() * sizeof(Vertex));
    if (bytes > vbCapacity_) {
        vb_.Reset();
        D3D11_BUFFER_DESC vd{};
        vd.ByteWidth = bytes;
        vd.Usage = D3D11_USAGE_DYNAMIC;
        vd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        vd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        if (FAILED(dev_->CreateBuffer(&vd, nullptr, &vb_))) {
            vbCapacity_ = 0;
            return false;
        }
        vbCapacity_ = bytes;
    }
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(vb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) return false;
    std::memcpy(m.pData, tri.data(), bytes);
    ctx->Unmap(vb_.Get(), 0);
    const bool toSdr = scRgb_ && dd.Format != DXGI_FORMAT_R16G16B16A16_FLOAT;
    const ParamsCb pc{{float(w), float(h)}, toSdr ? 1.0f : 0.0f, white_};
    if (SUCCEEDED(ctx->Map(cb_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m))) {
        std::memcpy(m.pData, &pc, sizeof pc);
        ctx->Unmap(cb_.Get(), 0);
    }
    const D3D11_VIEWPORT vp{0, 0, float(w), float(h), 0, 1};
    const UINT stride = sizeof(Vertex), offset = 0;
    ID3D11RenderTargetView* target = rtv.Get();
    ID3D11Buffer* vb = vb_.Get();
    ID3D11Buffer* cbuf = cb_.Get();
    ID3D11ShaderResourceView* srv = srv_.Get();
    ID3D11SamplerState* smp = sampler_.Get();
    ctx->OMSetRenderTargets(1, &target, nullptr);
    ctx->OMSetBlendState(blend_.Get(), nullptr, 0xffffffff);
    ctx->RSSetState(raster_.Get());
    ctx->RSSetViewports(1, &vp);
    ctx->IASetInputLayout(layout_.Get());
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
    ctx->VSSetShader(vs_.Get(), nullptr, 0);
    ctx->VSSetConstantBuffers(0, 1, &cbuf);
    ctx->PSSetShader(ps_.Get(), nullptr, 0);
    ctx->PSSetConstantBuffers(0, 1, &cbuf);
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, &smp);
    ctx->Draw(UINT(tri.size()), 0);
    ID3D11ShaderResourceView* none = nullptr;
    ctx->PSSetShaderResources(0, 1, &none);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

std::vector<std::uint8_t> genieRenderToBgra(const BgraImage& src, const std::vector<GenieVertex>& mesh, UINT w, UINT h,
                                            POINT origin) {
    using Microsoft::WRL::ComPtr;
    std::vector<std::uint8_t> out;
    if (src.w <= 0 || src.h <= 0 || src.px.size() < std::size_t(src.w) * src.h * 4 || !w || !h) return out;
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                 D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
        return out;
    std::vector<std::uint8_t> pm(src.px.begin(), src.px.begin() + std::ptrdiff_t(src.w) * src.h * 4);
    for (std::size_t i = 0; i < pm.size(); i += 4)   // la capture de DWM est prémultipliée
        for (int c = 0; c < 3; ++c) pm[i + c] = std::uint8_t((pm[i + c] * pm[i + 3] + 127) / 255);
    D3D11_TEXTURE2D_DESC d{};
    d.Width = UINT(src.w);
    d.Height = UINT(src.h);
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA init{pm.data(), UINT(src.w * 4), 0};
    ComPtr<ID3D11Texture2D> frame, dst, stage;
    if (FAILED(dev->CreateTexture2D(&d, &init, &frame))) return out;
    d.Width = w;
    d.Height = h;
    d.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &dst))) return out;
    d.BindFlags = 0;
    d.Usage = D3D11_USAGE_STAGING;
    d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &stage))) return out;
    GenieGpu gpu;
    if (!gpu.init(dev.Get()) || !gpu.setSource(ctx.Get(), frame.Get(), UINT(src.w), UINT(src.h)) ||
        !gpu.draw(ctx.Get(), dst.Get(), w, h, origin, mesh))
        return out;
    ctx->CopyResource(stage.Get(), dst.Get());
    D3D11_MAPPED_SUBRESOURCE m{};
    if (FAILED(ctx->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &m))) return out;
    out.resize(std::size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y)
        std::memcpy(out.data() + std::size_t(y) * w * 4, static_cast<const std::uint8_t*>(m.pData) + std::size_t(y) * m.RowPitch, w * 4);
    ctx->Unmap(stage.Get(), 0);
    return out;
}

} // namespace md
