#include "bar_renderer.h"

#include <dxgi1_2.h>

#include <algorithm>
#include <cmath>

#include "../core/log.h"
#include "../glass/backdrop_capture.h"
#include "../popup/glyphs.h"

namespace md {
namespace {

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

} // namespace

bool isDeviceLost(HRESULT hr) {
    return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG ||
           hr == D2DERR_RECREATE_TARGET;
}

bool BarRenderer::createFactories() {
    if (d2d_) return true;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(d2d_.GetAddressOf()))) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_)))) {
        d2d_.Reset();
        return false;
    }
    return true;
}

bool BarRenderer::init(HWND hwnd) {
    if (!createFactories()) return false;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    auto adapter = BackdropCapture::adapterFor(mon);   // même carte que la capture des menus en verre
    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
    if (FAILED(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                 D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, UINT(std::size(levels)), D3D11_SDK_VERSION,
                                 &d3d_, nullptr, nullptr))) {
        log::warn(L"Barre : device matériel indisponible, WARP");
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                     UINT(std::size(levels)), D3D11_SDK_VERSION, &d3d_, nullptr, nullptr)))
            return false;
    }
    Com<IDXGIDevice> dxgi;
    if (FAILED(d3d_.As(&dxgi)) || FAILED(d2d_->CreateDevice(dxgi.Get(), &d2dDevice_)) ||
        FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc_)) ||
        FAILED(DCompositionCreateDevice2(d2dDevice_.Get(), IID_PPV_ARGS(&dcomp_))) ||
        FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &target_)) || FAILED(dcomp_->CreateVisual(&visual_)))
        return false;
    target_->SetRoot(visual_.Get());
    return true;
}

bool BarRenderer::initOffscreen() { return createFactories(); }

void BarRenderer::resize(UINT w, UINT h) {
    width_ = std::max(1u, w);
    height_ = std::max(1u, h);
    if (!dcomp_) return;
    surface_.Reset();
    if (FAILED(dcomp_->CreateSurface(width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                     &surface_))) {
        log::error(L"Barre : CreateSurface %ux%u a échoué", width_, height_);
        return;
    }
    visual_->SetContent(surface_.Get());
    dcomp_->Commit();
}

std::wstring BarRenderer::setFont(const std::wstring& wanted, float sizePx) {
    if (!dwrite_) return {};
    Com<IDWriteFontCollection> fonts;
    dwrite_->GetSystemFontCollection(&fonts, FALSE);
    std::vector<std::wstring> candidates;
    if (!wanted.empty()) candidates.push_back(wanted);
    for (auto* f : {L"SF Pro Text", L"SF Pro", L"Inter", L"Segoe UI Variable Text", L"Segoe UI"}) candidates.push_back(f);
    family_ = L"Segoe UI";
    for (auto& c : candidates) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(c.c_str(), &index, &exists)) && exists) {
            family_ = c;
            break;
        }
    }
    sizePx_ = sizePx;
    regular_.Reset();
    bold_.Reset();
    dwrite_->CreateTextFormat(family_.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, sizePx, L"", &regular_);
    dwrite_->CreateTextFormat(family_.c_str(), nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL,
                              DWRITE_FONT_STRETCH_NORMAL, sizePx, L"", &bold_);
    for (auto* f : {regular_.Get(), bold_.Get()})
        if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return family_;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> BarRenderer::layoutOf(const std::wstring& text, bool bold) {
    Com<IDWriteTextLayout> l;
    IDWriteTextFormat* fmt = bold ? bold_.Get() : regular_.Get();
    if (fmt) dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt, 10000, 1000, &l);
    return l;
}

float BarRenderer::measure(const std::wstring& text, bool bold) {
    auto l = layoutOf(text, bold);
    if (!l) return 0;
    DWRITE_TEXT_METRICS m{};
    l->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

void BarRenderer::draw(ID2D1RenderTarget* rt, const BarFrame& f, float height) {
    const float s = f.scale;
    const MenuBarMetrics& m = f.metrics;
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);   // fond transparent : pas de ClearType
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    Com<ID2D1SolidColorBrush> ink, shadow, capsule;
    rt->CreateSolidColorBrush(f.darkText ? rgba(0, 0, 0, 0.85f) : rgba(1, 1, 1, 1), &ink);
    rt->CreateSolidColorBrush(rgba(0, 0, 0, 0.25f), &shadow);
    rt->CreateSolidColorBrush(f.darkText ? rgba(0, 0, 0, 0.10f) : rgba(1, 1, 1, 0.22f), &capsule);
    const float shadowDy = std::max(1.0f, std::round(s));
    const bool withShadow = !f.darkText;

    for (const auto& it : f.items) {
        if (it.highlighted) {
            const float hh = float(m.highlightHeight) * s, r = float(m.highlightRadius) * s;
            const float top = (height - hh) / 2;
            rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(it.x, top, it.x + it.width, top + hh), r, r),
                                     capsule.Get());
        }
        if (it.logo) {
            const float size = float(m.logoSize) * s;
            if (!logo_.bgra.empty() && logo_.w && logo_.h) {
                if (logoBitmapStale_ || logoOwner_ != rt) {
                    logoBitmap_.Reset();
                    auto props = D2D1::BitmapProperties(
                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                    rt->CreateBitmap(D2D1::SizeU(logo_.w, logo_.h), logo_.bgra.data(), logo_.w * 4, props, &logoBitmap_);
                    logoOwner_ = rt;
                    logoBitmapStale_ = false;
                }
                if (logoBitmap_) {
                    const float w = size * float(logo_.w) / float(logo_.h);
                    const float x0 = it.x + (it.width - w) / 2, y0 = (height - size) / 2;
                    const D2D1_RECT_F src = D2D1::RectF(0, 0, float(logo_.w), float(logo_.h));
                    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);   // exigé par FillOpacityMask
                    if (withShadow)
                        rt->FillOpacityMask(logoBitmap_.Get(), shadow.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
                                            D2D1::RectF(x0, y0 + shadowDy, x0 + w, y0 + size + shadowDy), src);
                    rt->FillOpacityMask(logoBitmap_.Get(), ink.Get(), D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
                                        D2D1::RectF(x0, y0, x0 + w, y0 + size), src);
                    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
                    continue;
                }
            }
            // Logo par défaut : quatre carrés (logo de Windows), à la couleur du texte.
            const float gap = size * 0.09f, q = (size - gap) / 2;
            const float x0 = std::round(it.x + (it.width - size) / 2), y0 = std::round((height - size) / 2);
            for (int pass = withShadow ? 0 : 1; pass < 2; ++pass) {
                const float dy = pass == 0 ? shadowDy : 0;
                ID2D1SolidColorBrush* b = pass == 0 ? shadow.Get() : ink.Get();
                for (int k = 0; k < 4; ++k) {
                    const float qx = x0 + (k % 2) * (q + gap), qy = y0 + dy + (k / 2) * (q + gap);
                    rt->FillRectangle(D2D1::RectF(qx, qy, qx + q, qy + q), b);
                }
            }
            continue;
        }
        if (it.image && it.imageW && it.imageH && it.image->size() == std::size_t(it.imageW) * it.imageH * 4) {
            const float size = std::round(float(m.statusIconSize) * s);
            const float x0 = std::round(it.x + (it.width - size) / 2), y0 = std::round((height - size) / 2);
            Com<ID2D1Bitmap> bmp;
            const auto props = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(it.imageW, it.imageH), it.image->data(), it.imageW * 4, props, &bmp)))
                rt->DrawBitmap(bmp.Get(), D2D1::RectF(x0, y0, x0 + size, y0 + size), 1.0f,
                               D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            continue;
        }
        if (it.glyph != Glyph::None) {
            const float size = std::round(float(m.statusIconSize) * s);
            const float x0 = std::round(it.x + (it.width - size) / 2), y0 = std::round((height - size) / 2);
            if (withShadow)
                drawGlyph(rt, it.glyph, D2D1::RectF(x0, y0 + shadowDy, x0 + size, y0 + size + shadowDy), shadow.Get(),
                          it.level, it.alt);
            drawGlyph(rt, it.glyph, D2D1::RectF(x0, y0, x0 + size, y0 + size), ink.Get(), it.level, it.alt);
            continue;
        }
        auto l = layoutOf(it.text, it.bold);
        if (!l) continue;
        DWRITE_TEXT_METRICS tm{};
        l->GetMetrics(&tm);
        const float x = std::round(it.x + (it.width - tm.widthIncludingTrailingWhitespace) / 2);
        const float y = std::round((height - tm.height) / 2);
        if (withShadow) rt->DrawTextLayout({x, y + shadowDy}, l.Get(), shadow.Get());
        rt->DrawTextLayout({x, y}, l.Get(), ink.Get());
    }
}

void BarRenderer::reset() {
    surface_.Reset();
    visual_.Reset();
    target_.Reset();
    dcomp_.Reset();
    dc_.Reset();
    d2dDevice_.Reset();
    d3d_.Reset();
    logoBitmap_.Reset();
    logoOwner_ = nullptr;
    logoBitmapStale_ = true;
}

bool BarRenderer::render(const BarFrame& f) {
    lastError_ = S_OK;
    if (!surface_) return false;
    POINT offset{};
    Com<ID2D1DeviceContext> d;
    if (HRESULT hr = surface_->BeginDraw(nullptr, IID_PPV_ARGS(&d), &offset); FAILED(hr)) {
        lastError_ = hr;
        return false;
    }
    d->SetDpi(96, 96);
    d->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    d->Clear(rgba(0, 0, 0, 0));
    draw(d.Get(), f, float(height_));
    if (logoOwner_ == d.Get()) logoOwner_ = nullptr;   // contexte propre à ce BeginDraw : recréer au prochain
    HRESULT hr = surface_->EndDraw();
    if (SUCCEEDED(hr)) hr = dcomp_->Commit();
    if (SUCCEEDED(hr) && d3d_) hr = d3d_->GetDeviceRemovedReason();
    lastError_ = hr;
    return SUCCEEDED(hr);
}

bool BarRenderer::renderToImage(const BarFrame& f, const std::vector<std::uint8_t>& background, UINT w, UINT h,
                                std::vector<std::uint8_t>& out) {
    if (!wic_ || !d2d_ || background.size() < std::size_t(w) * h * 4) return false;
    Com<IWICBitmap> bmp;
    if (FAILED(wic_->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, UINT(w * h * 4),
                                            const_cast<BYTE*>(background.data()), &bmp)))
        return false;
    Com<ID2D1RenderTarget> rt;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                              96, 96);
    if (FAILED(d2d_->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return false;
    rt->BeginDraw();
    draw(rt.Get(), f, float(h));
    if (FAILED(rt->EndDraw())) return false;
    logoOwner_ = nullptr;
    logoBitmap_.Reset();
    out.resize(std::size_t(w) * h * 4);
    return SUCCEEDED(bmp->CopyPixels(nullptr, w * 4, UINT(out.size()), out.data()));
}

} // namespace md
