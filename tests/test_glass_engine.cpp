// Moteur Liquid Glass v2, rendu hors écran (carte graphique logicielle WARP) : formes qui fusionnent, lumière.
#include <windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "minitest.h"
#include "../src/glass/glass_renderer.h"

using Microsoft::WRL::ComPtr;

namespace {

struct Bench {
    ComPtr<ID3D11Device> dev;
    ComPtr<ID3D11DeviceContext> ctx;
    ComPtr<ID3D11ShaderResourceView> backdrop;
    ComPtr<ID3D11Texture2D> target, staging;
    ComPtr<ID3D11RenderTargetView> rtv;
    md::GlassRenderer glass;
    static constexpr UINT W = 256, H = 128;

    bool init() {
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
                                     D3D11_SDK_VERSION, &dev, nullptr, &ctx)))
            return false;
        std::vector<std::uint32_t> gray(W * H, 0xFF808080u);
        D3D11_TEXTURE2D_DESC d{};
        d.Width = W;
        d.Height = H;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{gray.data(), W * 4, 0};
        ComPtr<ID3D11Texture2D> back;
        if (FAILED(dev->CreateTexture2D(&d, &init, &back)) || FAILED(dev->CreateShaderResourceView(back.Get(), nullptr, &backdrop)))
            return false;
        d.BindFlags = D3D11_BIND_RENDER_TARGET;
        if (FAILED(dev->CreateTexture2D(&d, nullptr, &target)) || FAILED(dev->CreateRenderTargetView(target.Get(), nullptr, &rtv)))
            return false;
        d.BindFlags = 0;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        return SUCCEEDED(dev->CreateTexture2D(&d, nullptr, &staging)) && glass.init(dev.Get());
    }
    // Alpha (0..255) du pixel (x, y) après le rendu des formes.
    std::vector<std::uint32_t> render(const std::vector<md::GlassShape>& shapes, const md::GlassParams& p) {
        glass.render(ctx.Get(), backdrop.Get(), W, H, rtv.Get(), shapes, p);
        ctx->CopyResource(staging.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE m{};
        std::vector<std::uint32_t> px(W * H);
        if (SUCCEEDED(ctx->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &m))) {
            for (UINT y = 0; y < H; ++y)
                std::memcpy(&px[y * W], static_cast<const std::uint8_t*>(m.pData) + y * m.RowPitch, W * 4);
            ctx->Unmap(staging.Get(), 0);
        }
        return px;
    }
};

int alphaOf(const std::vector<std::uint32_t>& px, int x, int y) { return int(px[std::size_t(y) * Bench::W + x] >> 24); }

std::vector<md::GlassShape> twoSquares() {
    md::GlassShape a;
    a.left = 40;
    a.top = 34;
    a.right = 100;
    a.bottom = 94;
    a.radius = 16;
    md::GlassShape b = a;
    b.left = 116;   // 16 pixels d'écart
    b.right = 176;
    return {a, b};
}

} // namespace

TEST_CASE(glass_engine_shapes_merge_like_liquid_glass) {
    Bench b;
    if (!b.init()) {
        CHECK(false);   // WARP est toujours présent sous Windows 10 et 11
        return;
    }
    md::GlassParams p;
    const auto apart = b.render(twoSquares(), p);
    CHECK(alphaOf(apart, 70, 64) == 255);   // dans le premier carré
    CHECK(alphaOf(apart, 108, 64) == 0);    // entre les deux : vide
    p.merge = 28;                           // les deux gouttes de verre se rejoignent
    const auto merged = b.render(twoSquares(), p);
    CHECK(alphaOf(merged, 108, 64) > 200);  // un pont de verre entre elles
    CHECK(alphaOf(merged, 108, 20) == 0);   // mais pas au-dessus d'elles
    CHECK(alphaOf(merged, 70, 64) == 255);
}

TEST_CASE(glass_engine_light_direction_moves_the_highlight) {
    Bench b;
    if (!b.init()) {
        CHECK(false);
        return;
    }
    std::vector<md::GlassShape> one = {twoSquares().front()};
    md::GlassParams p;
    p.specular = 1;
    auto bright = [](const std::vector<std::uint32_t>& px, int x, int y) { return int((px[std::size_t(y) * Bench::W + x] >> 16) & 0xFF); };
    const auto top = b.render(one, p);   // lumière par défaut : d'en haut
    CHECK(bright(top, 70, 34) > bright(top, 70, 93));
    p.lightX = 0;
    p.lightY = -1;   // lumière d'en bas : le reflet passe en bas
    const auto below = b.render(one, p);
    CHECK(bright(below, 70, 93) > bright(below, 70, 34));
}
