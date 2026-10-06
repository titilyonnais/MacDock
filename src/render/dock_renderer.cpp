#include "dock_renderer.h"

#include <d2d1_3helper.h>
#include <dxgi1_3.h>

#include <algorithm>
#include <cmath>

#include "../calib/png_io.h"
#include "../core/log.h"
#include "../geom/smooth_rect.h"

namespace md {

namespace {
D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }
}

bool DockRenderer::createDevices(bool warpOnly) {
    bitmaps_.clear();
    geometries_.clear();
    surface_.Reset();
    shadow_.Reset();
    target_.Reset();
    visual_.Reset();
    dcomp_.Reset();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    if (!warpOnly)
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                               d3d_.ReleaseAndGetAddressOf(), nullptr, nullptr);
    if (FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                               d3d_.ReleaseAndGetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) { log::error(L"D3D11CreateDevice a échoué (0x%08X)", hr); return false; }

    Com<IDXGIDevice> dxgi;
    d3d_.As(&dxgi);
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(d2dFactory_.ReleaseAndGetAddressOf()))))
        return false;
    if (FAILED(d2dFactory_->CreateDevice(dxgi.Get(), d2dDevice_.ReleaseAndGetAddressOf()))) return false;
    if (FAILED(d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, dc_.ReleaseAndGetAddressOf())))
        return false;
    if (!dwrite_ && FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3),
                                               reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))))
        return false;
    dc_->CreateEffect(CLSID_D2D1Shadow, &shadow_);
    return true;
}

bool DockRenderer::init(HWND hwnd) {
    hwnd_ = hwnd;
    if (!createDevices(false)) return false;
    if (FAILED(DCompositionCreateDevice2(d2dDevice_.Get(), IID_PPV_ARGS(&dcomp_)))) return false;
    if (FAILED(dcomp_->CreateTargetForHwnd(hwnd, TRUE, &target_))) return false;
    if (FAILED(dcomp_->CreateVisual(&visual_))) return false;
    target_->SetRoot(visual_.Get());
    if (width_ && height_) resize(width_, height_);
    return true;
}

bool DockRenderer::initOffscreen() {
    hwnd_ = nullptr;
    return createDevices(true);
}

void DockRenderer::resize(UINT w, UINT h) {
    width_ = std::max(1u, w);
    height_ = std::max(1u, h);
    if (!dcomp_) return;
    surface_.Reset();
    if (FAILED(dcomp_->CreateSurface(width_, height_, DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED,
                                     &surface_))) {
        log::error(L"CreateSurface %ux%u a échoué", width_, height_);
        return;
    }
    visual_->SetContent(surface_.Get());
    dcomp_->Commit();
}

std::wstring DockRenderer::resolveFont(const std::wstring& wanted) {
    if (wanted == fontWanted_ && !fontResolved_.empty()) return fontResolved_;
    fontWanted_ = wanted;
    Com<IDWriteFontCollection> fonts;
    dwrite_->GetSystemFontCollection(&fonts, FALSE);
    std::vector<std::wstring> candidates;
    if (!wanted.empty()) candidates.push_back(wanted);
    for (auto* f : {L"SF Pro Text", L"SF Pro", L"Inter", L"Segoe UI Variable Text", L"Segoe UI"}) candidates.push_back(f);
    for (auto& c : candidates) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(c.c_str(), &index, &exists)) && exists) {
            fontResolved_ = c;
            log::info(L"Police utilisée : %s", c.c_str());
            return c;
        }
    }
    fontResolved_ = L"Segoe UI";
    return fontResolved_;
}

ID2D1Geometry* DockRenderer::smoothRect(D2D1_RECT_F r, float radius) {
    auto q = [](float v) { return long(std::lround(v * 64)); };   // clé arrondie à 1/64 px
    std::array<long, 5> key{q(r.left), q(r.top), q(r.right), q(r.bottom), q(radius)};
    for (auto& g : geometries_)
        if (g.key == key) return g.geometry.Get();

    Com<ID2D1PathGeometry> path;
    Com<ID2D1GeometrySink> sink;
    if (FAILED(d2dFactory_->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return nullptr;
    auto pts = smoothRectOutline(r.left, r.top, r.right - r.left, r.bottom - r.top, radius, 12);
    sink->BeginFigure(D2D1::Point2F(float(pts[0].x), float(pts[0].y)), D2D1_FIGURE_BEGIN_FILLED);
    for (size_t i = 1; i < pts.size(); ++i) sink->AddLine(D2D1::Point2F(float(pts[i].x), float(pts[i].y)));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (FAILED(sink->Close())) return nullptr;

    if (geometries_.size() >= 8) geometries_.erase(geometries_.begin());
    geometries_.push_back({key, path});
    return path.Get();
}

ID2D1Bitmap1* DockRenderer::bitmapFor(const IconProvider::ImagePtr& img) {
    if (!img || img->size <= 0) return nullptr;
    if (auto it = bitmaps_.find(img.get()); it != bitmaps_.end() && !it->second.owner.expired())
        return it->second.bitmap.Get();
    // Purge des images disparues (cache de l'IconProvider vidé).
    std::erase_if(bitmaps_, [](auto& kv) { return kv.second.owner.expired(); });
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    Com<ID2D1Bitmap1> bmp;
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(UINT(img->size), UINT(img->size)), img->bgra.data(),
                                 UINT(img->size * 4), props, &bmp)))
        return nullptr;
    bitmaps_[img.get()] = {img, bmp};
    return bmp.Get();
}

void DockRenderer::drawBackground(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m) {
    D2D1_RECT_F rect = D2D1::RectF(f.bgLeft, f.bgTop, f.bgRight, f.bgBottom);
    ID2D1Geometry* shape = smoothRect(rect, f.cornerRadius);
    if (!shape) return;

    // Ombre portée douce.
    if (shadow_ && m.shadowOpacity > 0) {
        Com<ID2D1CommandList> list;
        Com<ID2D1Image> previous;
        dc->GetTarget(&previous);
        if (SUCCEEDED(dc->CreateCommandList(&list))) {
            dc->SetTarget(list.Get());
            Com<ID2D1SolidColorBrush> black;
            dc->CreateSolidColorBrush(rgba(0, 0, 0, 1), &black);
            dc->FillGeometry(shape, black.Get());
            list->Close();
            dc->SetTarget(previous.Get());
            shadow_->SetInput(0, list.Get());
            shadow_->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, float(m.shadowBlur * f.scale / 3));
            shadow_->SetValue(D2D1_SHADOW_PROP_COLOR,
                              D2D1::Vector4F(0, 0, 0, float(m.shadowOpacity * (f.dark ? 1.6 : 1.0))));
            D2D1_POINT_2F offset{0, 2 * f.scale};
            dc->DrawImage(shadow_.Get(), &offset);
        } else {
            dc->SetTarget(previous.Get());
        }
    }

    // Verre dépoli (repli sans capture) : dégradé vertical translucide.
    float base = float(f.dark ? m.bgOpacityDark : m.bgOpacityLight);
    D2D1_GRADIENT_STOP stops[2];
    if (f.dark) {
        stops[0] = {0, rgba(0.24f, 0.24f, 0.26f, base + 0.10f)};
        stops[1] = {1, rgba(0.12f, 0.12f, 0.13f, base + 0.18f)};
    } else {
        stops[0] = {0, rgba(1, 1, 1, base + 0.18f)};
        stops[1] = {1, rgba(0.93f, 0.93f, 0.95f, base + 0.06f)};
    }
    Com<ID2D1GradientStopCollection> coll;
    dc->CreateGradientStopCollection(stops, 2, &coll);
    Com<ID2D1LinearGradientBrush> fill;
    dc->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, f.bgTop),
                                                                      D2D1::Point2F(0, f.bgBottom)),
                                  coll.Get(), &fill);
    dc->FillGeometry(shape, fill.Get());

    // Liseré intérieur clair : contour d'une forme réduite d'un demi-trait, de même famille.
    float stroke = std::max(1.0f, f.scale);
    D2D1_RECT_F inner = D2D1::RectF(rect.left + stroke / 2, rect.top + stroke / 2, rect.right - stroke / 2,
                                    rect.bottom - stroke / 2);
    if (ID2D1Geometry* rim = smoothRect(inner, std::max(0.0f, f.cornerRadius - stroke / 2))) {
        Com<ID2D1SolidColorBrush> rimBrush;
        dc->CreateSolidColorBrush(rgba(1, 1, 1, float(m.borderOpacity) * (f.dark ? 0.35f : 1.0f)), &rimBrush);
        dc->DrawGeometry(rim, rimBrush.Get(), stroke);
    }
}

void DockRenderer::drawTooltip(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m,
                               const std::wstring& font) {
    const auto& t = f.tooltip;
    if (!t.visible || t.text.empty() || t.opacity <= 0.01f) return;
    Com<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(resolveFont(font).c_str(), nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         float(m.tooltipFontSize) * f.scale, L"", &format)))
        return;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    Com<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(t.text.c_str(), UINT32(t.text.size()), format.Get(), 4000, 200, &layout)))
        return;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    float padX = float(m.tooltipPadX) * f.scale, padY = float(m.tooltipPadY) * f.scale;
    float w = tm.width + 2 * padX, h = tm.height + 2 * padY;
    float maxRight = float(width_ ? width_ : 4000);
    float left = std::clamp(t.cx - w / 2, 2.0f, std::max(2.0f, maxRight - w - 2));
    D2D1_RECT_F rect = D2D1::RectF(left, t.bottom - h, left + w, t.bottom);
    ID2D1Geometry* shape = smoothRect(rect, h / 2);   // capsule à coins continus
    if (!shape) return;

    Com<ID2D1SolidColorBrush> bg, border, text;
    if (f.dark) {
        dc->CreateSolidColorBrush(rgba(0.16f, 0.16f, 0.17f, 0.88f * t.opacity), &bg);
        dc->CreateSolidColorBrush(rgba(1, 1, 1, 0.16f * t.opacity), &border);
        dc->CreateSolidColorBrush(rgba(1, 1, 1, 0.92f * t.opacity), &text);
    } else {
        dc->CreateSolidColorBrush(rgba(0.97f, 0.97f, 0.98f, 0.90f * t.opacity), &bg);
        dc->CreateSolidColorBrush(rgba(0, 0, 0, 0.10f * t.opacity), &border);
        dc->CreateSolidColorBrush(rgba(0, 0, 0, 0.85f * t.opacity), &text);
    }
    dc->FillGeometry(shape, bg.Get());
    dc->DrawGeometry(shape, border.Get(), std::max(1.0f, f.scale * 0.75f));
    dc->DrawTextLayout(D2D1::Point2F(left + padX, rect.top + padY), layout.Get(), text.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

bool DockRenderer::render(const RenderFrame& f, const Metrics& m, const std::wstring& fontFamily) {
    if (!surface_) return false;
    POINT offset{};
    Com<ID2D1DeviceContext> dc;
    HRESULT hr = surface_->BeginDraw(nullptr, IID_PPV_ARGS(&dc), &offset);
    if (FAILED(hr)) {
        log::warn(L"BeginDraw a échoué (0x%08X)", hr);
        return hr != DXGI_ERROR_DEVICE_REMOVED && hr != DXGI_ERROR_DEVICE_RESET && hr != D2DERR_RECREATE_TARGET;
    }
    dc->SetDpi(96, 96);
    dc->SetTransform(D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y)));
    dc->Clear(rgba(0, 0, 0, 0));
    drawFrame(dc.Get(), f, m, fontFamily);

    hr = surface_->EndDraw();
    if (FAILED(hr)) {
        log::warn(L"EndDraw a échoué (0x%08X)", hr);
        return false;
    }
    hr = dcomp_->Commit();
    return SUCCEEDED(hr);
}

void DockRenderer::drawFrame(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m,
                             const std::wstring& fontFamily) {
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    drawBackground(dc, f, m);

    Com<ID2D1SolidColorBrush> sepBrush, dotBrush;
    dc->CreateSolidColorBrush(f.dark ? rgba(1, 1, 1, 0.25f) : rgba(0, 0, 0, 0.22f), &sepBrush);
    dc->CreateSolidColorBrush(f.dark ? rgba(1, 1, 1, 0.80f) : rgba(0, 0, 0, 0.78f), &dotBrush);

    for (auto& icon : f.icons) {
        if (icon.separator) {
            float half = icon.sepLength / 2;
            dc->DrawLine(D2D1::Point2F(icon.cx, icon.cy - half), D2D1::Point2F(icon.cx, icon.cy + half),
                         sepBrush.Get(), std::max(1.0f, float(m.separatorWidth) * f.scale));
            continue;
        }
        if (ID2D1Bitmap1* bmp = bitmapFor(icon.image)) {
            float h = icon.size / 2;
            D2D1_RECT_F dst = D2D1::RectF(icon.cx - h, icon.cy - h, icon.cx + h, icon.cy + h);
            dc->DrawBitmap(bmp, dst, icon.opacity, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
        }
        if (icon.indicator) {
            float r = float(m.indicatorDiameter) * f.scale / 2;
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(icon.cx, icon.indicatorY), r, r), dotBrush.Get());
        }
    }

    drawTooltip(dc, f, m, fontFamily);
}

std::vector<std::uint8_t> DockRenderer::renderToBgra(const RenderFrame& f, const Metrics& m,
                                                     const std::wstring& fontFamily, UINT w, UINT h,
                                                     const std::vector<std::uint8_t>& wallpaper) {
    if (!dc_ || !w || !h) return {};
    auto pf = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    Com<ID2D1Bitmap1> target, readback, wall;
    if (FAILED(dc_->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0,
                                 D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, pf), &target)) ||
        FAILED(dc_->CreateBitmap(D2D1::SizeU(w, h), nullptr, 0,
                                 D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, pf),
                                 &readback)))
        return {};
    if (wallpaper.size() == size_t(w) * h * 4 &&
        FAILED(dc_->CreateBitmap(D2D1::SizeU(w, h), wallpaper.data(), w * 4, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, pf),
                                 &wall)))
        return {};

    const UINT savedW = width_;
    width_ = w;
    dc_->SetTarget(target.Get());
    dc_->BeginDraw();
    dc_->SetDpi(96, 96);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (wall) {
        dc_->DrawBitmap(wall.Get());
    } else {
        // Fond de bureau factice : dégradé coloré pour juger la translucidité.
        D2D1_GRADIENT_STOP stops[3] = {{0, rgba(0.16f, 0.32f, 0.62f, 1)}, {0.5f, rgba(0.55f, 0.36f, 0.66f, 1)},
                                       {1, rgba(0.95f, 0.55f, 0.42f, 1)}};
        Com<ID2D1GradientStopCollection> coll;
        dc_->CreateGradientStopCollection(stops, 3, &coll);
        Com<ID2D1LinearGradientBrush> brush;
        dc_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(float(w), float(h))), coll.Get(), &brush);
        dc_->FillRectangle(D2D1::RectF(0, 0, float(w), float(h)), brush.Get());
    }
    drawFrame(dc_.Get(), f, m, fontFamily);
    HRESULT hr = dc_->EndDraw();
    dc_->SetTarget(nullptr);
    width_ = savedW;
    if (FAILED(hr) || FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return {};

    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
    std::vector<std::uint8_t> out(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y) std::copy_n(mapped.bits + size_t(y) * mapped.pitch, size_t(w) * 4, &out[size_t(y) * w * 4]);
    readback->Unmap();
    return out;
}

bool DockRenderer::renderToFile(const RenderFrame& f, const Metrics& m, const std::wstring& fontFamily, UINT w,
                                UINT h, const std::wstring& path, const std::vector<std::uint8_t>& wallpaper) {
    auto px = renderToBgra(f, m, fontFamily, w, h, wallpaper);
    return !px.empty() && writePng(path, px.data(), w, h);
}

} // namespace md
