#include "sprite_renderer.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace md {

bool SpriteRenderer::init() {
    if (d2d_ && wic_ && dwrite_) return true;
    D2D1_FACTORY_OPTIONS opts{};
    if (!d2d_ && FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &opts,
                                          reinterpret_cast<void**>(d2d_.GetAddressOf()))))
        return false;
    if (!wic_ && FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_))))
        return false;
    if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                               reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    return true;
}

Microsoft::WRL::ComPtr<ID2D1RenderTarget> SpriteRenderer::begin(UINT w, UINT h, Com<IWICBitmap>& bitmap) {
    if (!init() || !w || !h) return nullptr;
    if (FAILED(wic_->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnDemand, &bitmap))) return nullptr;
    auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                                              D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    Com<ID2D1RenderTarget> rt;
    if (FAILED(d2d_->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt))) return nullptr;
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    return rt;
}

std::vector<std::uint8_t> SpriteRenderer::read(IWICBitmap* bitmap, UINT w, UINT h) {
    std::vector<std::uint8_t> out(size_t(w) * h * 4);
    WICRect rc{0, 0, INT(w), INT(h)};
    if (FAILED(bitmap->CopyPixels(&rc, w * 4, UINT(out.size()), out.data()))) out.clear();
    return out;
}

std::vector<std::uint8_t> SpriteRenderer::dragSprite(const IconProvider::Image& icon, UINT iconPx,
                                                     const std::wstring& label, float scale, bool dark,
                                                     const std::wstring& font, UINT& w, UINT& h) {
    w = h = 0;
    if (!init() || icon.size <= 0 || icon.bgra.size() < size_t(icon.size) * icon.size * 4 || !iconPx) return {};
    scale = scale > 0 ? scale : 1;

    // Mise en page de l'étiquette (capsule sous l'icône).
    Com<IDWriteTextLayout> layout;
    float labelW = 0, labelH = 0, padX = 11 * scale, padY = 5 * scale, gap = 6 * scale;
    if (!label.empty()) {
        Com<IDWriteTextFormat> format;
        if (SUCCEEDED(dwrite_->CreateTextFormat(font.empty() ? L"Segoe UI" : font.c_str(), nullptr,
                                                DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, 13 * scale, L"", &format)) &&
            SUCCEEDED(dwrite_->CreateTextLayout(label.c_str(), UINT32(label.size()), format.Get(), 4000, 400, &layout))) {
            format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TEXT_METRICS tm{};
            layout->GetMetrics(&tm);
            labelW = std::ceil(tm.width + 2 * padX);
            labelH = std::ceil(tm.height + 2 * padY);
        }
    }
    w = std::max(iconPx, UINT(labelW) + 2);
    h = iconPx + (layout ? UINT(gap + labelH) + 2 : 0);

    Com<IWICBitmap> bitmap;
    auto rt = begin(w, h, bitmap);
    if (!rt) return {};
    Com<ID2D1Bitmap> bmp;
    auto bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (SUCCEEDED(rt->CreateBitmap(D2D1::SizeU(UINT(icon.size), UINT(icon.size)), icon.bgra.data(), UINT(icon.size) * 4,
                                   bp, &bmp))) {
        float left = (float(w) - float(iconPx)) / 2;
        rt->DrawBitmap(bmp.Get(), D2D1::RectF(left, 0, left + float(iconPx), float(iconPx)), 1,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
    if (layout) {
        float top = float(iconPx) + gap, left = (float(w) - labelW) / 2;
        D2D1_ROUNDED_RECT capsule{D2D1::RectF(left, top, left + labelW, top + labelH), labelH / 2, labelH / 2};
        Com<ID2D1SolidColorBrush> bg, border, text;
        rt->CreateSolidColorBrush(dark ? D2D1::ColorF(0.16f, 0.16f, 0.17f, 0.90f) : D2D1::ColorF(0.97f, 0.97f, 0.98f, 0.92f), &bg);
        rt->CreateSolidColorBrush(dark ? D2D1::ColorF(1, 1, 1, 0.16f) : D2D1::ColorF(0, 0, 0, 0.10f), &border);
        rt->CreateSolidColorBrush(dark ? D2D1::ColorF(1, 1, 1, 0.92f) : D2D1::ColorF(0, 0, 0, 0.85f), &text);
        rt->FillRoundedRectangle(capsule, bg.Get());
        rt->DrawRoundedRectangle(capsule, border.Get(), std::max(1.0f, scale * 0.5f));
        rt->DrawTextLayout(D2D1::Point2F(left + padX, top + padY), layout.Get(), text.Get());
    }
    if (FAILED(rt->EndDraw())) return {};
    return read(bitmap.Get(), w, h);
}

std::vector<std::uint8_t> SpriteRenderer::poofFrame(double t, UINT px) {
    Com<IWICBitmap> bitmap;
    auto rt = begin(px, px, bitmap);
    if (!rt) return {};
    t = std::clamp(t, 0.0, 1.0);
    const float c = float(px) / 2;
    // Bouffée : quelques volutes qui s'écartent du centre en grossissant, puis s'estompent.
    const float grow = float(1 - std::pow(1 - t, 3));            // sortie rapide, ralentie à la fin
    const float fade = float(std::pow(1 - t, 1.6));
    const float pop = float(std::min(1.0, t / 0.12));            // apparition très brève
    const int puffs = 7;
    for (int i = 0; i < puffs; ++i) {
        double angle = 2 * std::numbers::pi * i / puffs + 0.35 * i;
        float dist = float(px) * (0.06f + 0.26f * grow) * (0.8f + 0.2f * float((i * 37) % 5) / 4);
        float radius = float(px) * (0.10f + 0.12f * grow) * pop * (0.85f + 0.15f * float((i * 13) % 3) / 2);
        D2D1_POINT_2F p{c + dist * float(std::cos(angle)), c + dist * float(std::sin(angle))};
        D2D1_GRADIENT_STOP stops[3] = {{0.0f, D2D1::ColorF(0.98f, 0.98f, 0.98f, 0.95f * fade)},
                                       {0.65f, D2D1::ColorF(0.86f, 0.86f, 0.88f, 0.75f * fade)},
                                       {1.0f, D2D1::ColorF(0.70f, 0.70f, 0.72f, 0.0f)}};
        Com<ID2D1GradientStopCollection> coll;
        Com<ID2D1RadialGradientBrush> brush;
        if (FAILED(rt->CreateGradientStopCollection(stops, 3, &coll)) ||
            FAILED(rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(p, {}, radius, radius), coll.Get(),
                                                 &brush)))
            continue;
        rt->FillEllipse(D2D1::Ellipse(p, radius, radius), brush.Get());
    }
    if (FAILED(rt->EndDraw())) return {};
    return read(bitmap.Get(), px, px);
}

} // namespace md
