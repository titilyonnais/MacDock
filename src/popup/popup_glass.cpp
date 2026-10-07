#include "popup_glass.h"

#include <dxgi1_2.h>

#include <algorithm>

namespace md {

using Microsoft::WRL::ComPtr;

void ScreenBackdrop::start(HWND notify, UINT notifyMsg, HMONITOR mon, const RECT& monitorRect) {
    rc_ = monitorRect;
    capture_.start(notify, notifyMsg, mon, {monitorRect.left, monitorRect.top, monitorRect.right, monitorRect.bottom});
}

bool ScreenBackdrop::take(ID3D11Device* dev) {
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    bool scRgb = false;
    float white = 1;
    bool got = capture_.takeLatest(dev, ctx.Get(),
                                   [&](UINT w, UINT h, bool hdr) -> ID3D11Texture2D* {
                                       D3D11_TEXTURE2D_DESC d{};
                                       if (screen_) screen_->GetDesc(&d);
                                       if (!screen_ || d.Width != w || d.Height != h || scRgb_ != hdr) {
                                           screen_.Reset();
                                           D3D11_TEXTURE2D_DESC n{};
                                           n.Width = w;
                                           n.Height = h;
                                           n.MipLevels = n.ArraySize = 1;
                                           n.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
                                           n.SampleDesc.Count = 1;
                                           n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                                           if (FAILED(dev->CreateTexture2D(&n, nullptr, &screen_))) return nullptr;
                                           scRgb_ = hdr;
                                       }
                                       return screen_.Get();
                                   },
                                   scRgb, white);
    if (got) white_ = white;
    return got;
}

bool ScreenBackdrop::copyTo(ID3D11Device* dev, const RECT& wr, WindowBackdrop& out) const {
    if (!screen_) return false;
    const UINT W = UINT(wr.right - wr.left), H = UINT(wr.bottom - wr.top);
    D3D11_TEXTURE2D_DESC sd{};
    screen_->GetDesc(&sd);
    D3D11_TEXTURE2D_DESC d{};
    if (out.tex) out.tex->GetDesc(&d);
    if (!out.tex || d.Width != W || d.Height != H || d.Format != sd.Format) {
        out = {};
        D3D11_TEXTURE2D_DESC n{};
        n.Width = W;
        n.Height = H;
        n.MipLevels = n.ArraySize = 1;
        n.Format = sd.Format;
        n.SampleDesc.Count = 1;
        n.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(dev->CreateTexture2D(&n, nullptr, &out.tex)) ||
            FAILED(dev->CreateShaderResourceView(out.tex.Get(), nullptr, &out.srv))) {
            out = {};
            return false;
        }
    }
    // La marge d'ombre peut déborder de l'écran : seule l'intersection est copiée.
    const LONG l = std::max(wr.left, rc_.left), t = std::max(wr.top, rc_.top);
    const LONG r = std::min({wr.right, rc_.right, rc_.left + LONG(sd.Width)});
    const LONG b = std::min({wr.bottom, rc_.bottom, rc_.top + LONG(sd.Height)});
    if (r <= l || b <= t) return false;
    D3D11_BOX box{UINT(l - rc_.left), UINT(t - rc_.top), 0, UINT(r - rc_.left), UINT(b - rc_.top), 1};
    ComPtr<ID3D11DeviceContext> ctx;
    dev->GetImmediateContext(&ctx);
    ctx->CopySubresourceRegion(out.tex.Get(), 0, UINT(l - wr.left), UINT(t - wr.top), 0, screen_.Get(), 0, &box);
    out.scRgb = scRgb_;
    out.white = white_;
    out.valid = true;
    return true;
}

bool GlassTarget::ensure(ID3D11Device* dev, ID2D1DeviceContext* dc, UINT w, UINT h) {
    D3D11_TEXTURE2D_DESC gd{};
    if (tex) tex->GetDesc(&gd);
    if (tex && gd.Width == w && gd.Height == h) return true;
    tex.Reset();
    rtv.Reset();
    bitmap.Reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<IDXGISurface> surf;
    auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                         D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dev->CreateTexture2D(&d, nullptr, &tex)) || FAILED(dev->CreateRenderTargetView(tex.Get(), nullptr, &rtv)) ||
        FAILED(tex.As(&surf)) || FAILED(dc->CreateBitmapFromDxgiSurface(surf.Get(), &props, &bitmap))) {
        tex.Reset();
        rtv.Reset();
        bitmap.Reset();
        return false;
    }
    return true;
}

} // namespace md
