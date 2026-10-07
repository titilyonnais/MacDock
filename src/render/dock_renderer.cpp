#include "dock_renderer.h"

#include <d2d1_3helper.h>
#include <DirectXPackedVector.h>
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

bool DockRenderer::createDevices(bool warpOnly, IDXGIAdapter1* adapter) {
    bitmaps_.clear();
    overlayBitmap_.Reset();
    geometries_.clear();
    glassReady_ = false;
    backdropTex_.Reset();
    backdropSrv_.Reset();
    glassTex_.Reset();
    glassRtv_.Reset();
    glassBitmap_.Reset();
    tsDisjoint_.Reset();
    tsBegin_.Reset();
    tsEnd_.Reset();
    tsPending_ = false;
    surface_.Reset();
    shadow_.Reset();
    target_.Reset();
    visual_.Reset();
    dcomp_.Reset();

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = E_FAIL;
    warp_ = false;
    if (!warpOnly && adapter)
        hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                               d3d_.ReleaseAndGetAddressOf(), nullptr, nullptr);
    if (!warpOnly && FAILED(hr))
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                               d3d_.ReleaseAndGetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                               d3d_.ReleaseAndGetAddressOf(), nullptr, nullptr);
        warp_ = SUCCEEDED(hr);
    }
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
    glassReady_ = glass_.init(d3d_.Get());
    if (!glassReady_) log::warn(L"Verre indisponible : repli sur le verre dépoli");
    return true;
}

bool DockRenderer::init(HWND hwnd, IDXGIAdapter1* adapter) {
    hwnd_ = hwnd;
    if (!createDevices(false, adapter)) return false;
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

bool DockRenderer::tooltipLayout(const RenderFrame& f, const Metrics& m, const std::wstring& font, D2D1_RECT_F& rect,
                                 Com<IDWriteTextLayout>& layout) {
    const auto& t = f.tooltip;
    if (!t.visible || t.text.empty() || t.opacity <= 0.01f) return false;
    Com<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(resolveFont(font).c_str(), nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
                                         DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         float(m.tooltipFontSize) * f.scale, L"", &format)))
        return false;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    if (FAILED(dwrite_->CreateTextLayout(t.text.c_str(), UINT32(t.text.size()), format.Get(), 4000, 200, &layout)))
        return false;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    float padX = float(m.tooltipPadX) * f.scale, padY = float(m.tooltipPadY) * f.scale;
    float w = tm.width + 2 * padX, h = tm.height + 2 * padY;
    float maxRight = float(width_ ? width_ : 4000);
    float left = std::clamp(t.cx - w / 2, 2.0f, std::max(2.0f, maxRight - w - 2));
    rect = D2D1::RectF(left, t.bottom - h, left + w, t.bottom);
    return true;
}

void DockRenderer::drawTooltip(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m,
                               const std::wstring& font, bool glass) {
    D2D1_RECT_F rect{};
    Com<IDWriteTextLayout> layout;
    if (!tooltipLayout(f, m, font, rect, layout)) return;
    const float opacity = f.tooltip.opacity;
    Com<ID2D1SolidColorBrush> text;
    dc->CreateSolidColorBrush(f.dark ? rgba(1, 1, 1, 0.92f * opacity) : rgba(0, 0, 0, 0.85f * opacity), &text);
    if (!glass) {   // repli : capsule dépolie ; avec le verre, la capsule est déjà dans glassTex_
        ID2D1Geometry* shape = smoothRect(rect, (rect.bottom - rect.top) / 2);   // capsule à coins continus
        if (!shape) return;
        Com<ID2D1SolidColorBrush> bg, border;
        if (f.dark) {
            dc->CreateSolidColorBrush(rgba(0.16f, 0.16f, 0.17f, 0.88f * opacity), &bg);
            dc->CreateSolidColorBrush(rgba(1, 1, 1, 0.16f * opacity), &border);
        } else {
            dc->CreateSolidColorBrush(rgba(0.97f, 0.97f, 0.98f, 0.90f * opacity), &bg);
            dc->CreateSolidColorBrush(rgba(0, 0, 0, 0.10f * opacity), &border);
        }
        dc->FillGeometry(shape, bg.Get());
        dc->DrawGeometry(shape, border.Get(), std::max(1.0f, f.scale * 0.75f));
    }
    float padX = float(m.tooltipPadX) * f.scale, padY = float(m.tooltipPadY) * f.scale;
    dc->DrawTextLayout(D2D1::Point2F(rect.left + padX, rect.top + padY), layout.Get(), text.Get(),
                       D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT);
}

ID3D11Texture2D* DockRenderer::backdropTexture(UINT w, UINT h, bool scRgb) {
    if (!d3d_ || !w || !h) return nullptr;
    if (backdropTex_) {
        D3D11_TEXTURE2D_DESC d{};
        backdropTex_->GetDesc(&d);
        if (d.Width == w && d.Height == h && backdropScRgb_ == scRgb) return backdropTex_.Get();
    }
    backdropTex_.Reset();
    backdropSrv_.Reset();
    D3D11_TEXTURE2D_DESC d{};
    d.Width = w;
    d.Height = h;
    d.MipLevels = d.ArraySize = 1;
    d.Format = scRgb ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
    d.SampleDesc.Count = 1;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(d3d_->CreateTexture2D(&d, nullptr, &backdropTex_)) ||
        FAILED(d3d_->CreateShaderResourceView(backdropTex_.Get(), nullptr, &backdropSrv_))) {
        backdropTex_.Reset();
        backdropSrv_.Reset();
        return nullptr;
    }
    backdropScRgb_ = scRgb;
    return backdropTex_.Get();
}

bool DockRenderer::runGlass(const RenderFrame& f, const Metrics& m, const std::wstring& font, UINT w, UINT h) {
    if (!glassReady_ || !backdropSrv_ || !w || !h) return false;
    D3D11_TEXTURE2D_DESC bd{};
    backdropTex_->GetDesc(&bd);
    if (bd.Width != w || bd.Height != h) return false;

    // Cible du verre (taille de la fenêtre) et son image Direct2D.
    D3D11_TEXTURE2D_DESC gd{};
    if (glassTex_) glassTex_->GetDesc(&gd);
    if (!glassTex_ || gd.Width != w || gd.Height != h) {
        glassTex_.Reset();
        glassRtv_.Reset();
        glassBitmap_.Reset();
        D3D11_TEXTURE2D_DESC d{};
        d.Width = w;
        d.Height = h;
        d.MipLevels = d.ArraySize = 1;
        d.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        d.SampleDesc.Count = 1;
        d.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        Com<IDXGISurface> surface;
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(d3d_->CreateTexture2D(&d, nullptr, &glassTex_)) ||
            FAILED(d3d_->CreateRenderTargetView(glassTex_.Get(), nullptr, &glassRtv_)) ||
            FAILED(glassTex_.As(&surface)) || FAILED(dc_->CreateBitmapFromDxgiSurface(surface.Get(), &props, &glassBitmap_))) {
            glassTex_.Reset();
            glassRtv_.Reset();
            glassBitmap_.Reset();
            return false;
        }
    }

    const float s = f.scale;
    GlassParams p;
    p.scale = s;
    p.dark = f.dark;
    p.blurSigmaPx = float(m.glassBlur) * s;
    p.bevelPx = float(m.glassBevel) * s;
    p.refraction = float(m.glassRefraction);
    p.chromatic = float(m.glassChromatic);
    p.fresnel = float(m.glassFresnel);
    p.specular = float(m.glassSpecular);
    p.tint = float(f.dark ? m.glassTintDark : m.glassTintLight);
    p.saturation = float(m.glassSaturation);
    p.shadowBlurPx = float(m.shadowBlur) * s;
    p.shadowOffsetPx = 2 * s;
    p.backdropIsScRgb = backdropScRgb_;
    p.sdrWhiteScale = sdrWhite_;

    std::vector<GlassShape> shapes;
    float shadow = float(m.shadowOpacity * (f.dark ? 1.6 : 1.0));
    shapes.push_back({f.bgLeft, f.bgTop, f.bgRight, f.bgBottom,
                      float(limitedCornerRadius(f.bgRight - f.bgLeft, f.bgBottom - f.bgTop, f.cornerRadius)), 1, shadow, 1});
    D2D1_RECT_F tip{};
    Com<IDWriteTextLayout> layout;
    if (tooltipLayout(f, m, font, tip, layout)) {
        float th = tip.bottom - tip.top;
        shapes.push_back({tip.left, tip.top, tip.right, tip.bottom,
                          float(limitedCornerRadius(tip.right - tip.left, th, th / 2)), float(m.tooltipGlassStrength),
                          shadow * 0.6f, f.tooltip.opacity});
    }
    Com<ID3D11DeviceContext> ctx;
    d3d_->GetImmediateContext(&ctx);
    beginGpuTimer(ctx.Get());
    bool ok = glass_.render(ctx.Get(), backdropSrv_.Get(), w, h, glassRtv_.Get(), shapes, p);
    endGpuTimer(ctx.Get());
    return ok;
}

void DockRenderer::beginGpuTimer(ID3D11DeviceContext* ctx) {
    if (!gpuTiming_ || !d3d_) return;
    if (!tsDisjoint_) {
        D3D11_QUERY_DESC q{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
        D3D11_QUERY_DESC t{D3D11_QUERY_TIMESTAMP, 0};
        if (FAILED(d3d_->CreateQuery(&q, &tsDisjoint_)) || FAILED(d3d_->CreateQuery(&t, &tsBegin_)) ||
            FAILED(d3d_->CreateQuery(&t, &tsEnd_))) {
            tsDisjoint_.Reset();
            return;
        }
    }
    if (tsPending_) {   // mesure précédente : lue sans attendre ; perdue si pas encore prête
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT dj{};
        UINT64 b = 0, e = 0;
        if (ctx->GetData(tsDisjoint_.Get(), &dj, sizeof dj, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            ctx->GetData(tsBegin_.Get(), &b, sizeof b, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
            ctx->GetData(tsEnd_.Get(), &e, sizeof e, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK && !dj.Disjoint &&
            dj.Frequency) {
            gpuMsSum_ += double(e - b) * 1000.0 / double(dj.Frequency);
            ++gpuMsCount_;
        }
        tsPending_ = false;
    }
    ctx->Begin(tsDisjoint_.Get());
    ctx->End(tsBegin_.Get());
}

void DockRenderer::endGpuTimer(ID3D11DeviceContext* ctx) {
    if (!gpuTiming_ || !tsDisjoint_) return;
    ctx->End(tsEnd_.Get());
    ctx->End(tsDisjoint_.Get());
    tsPending_ = true;
}

LUID DockRenderer::adapterLuid() const {
    Com<IDXGIDevice> dxgi;
    Com<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC d{};
    if (d3d_ && SUCCEEDED(d3d_.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) && SUCCEEDED(adapter->GetDesc(&d)))
        return d.AdapterLuid;
    return {};
}

double DockRenderer::takeGlassGpuMs() {
    double ms = gpuMsCount_ ? gpuMsSum_ / gpuMsCount_ : -1;
    gpuMsSum_ = 0;
    gpuMsCount_ = 0;
    return ms;
}

bool DockRenderer::render(const RenderFrame& f, const Metrics& m, const std::wstring& fontFamily) {
    if (!surface_) return false;
    const bool glass = f.glass && runGlass(f, m, fontFamily, width_, height_);
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
    drawFrame(dc.Get(), f, m, fontFamily, glass);

    hr = surface_->EndDraw();
    if (FAILED(hr)) {
        log::warn(L"EndDraw a échoué (0x%08X)", hr);
        return false;
    }
    hr = dcomp_->Commit();
    return SUCCEEDED(hr);
}

void DockRenderer::drawFrame(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m,
                             const std::wstring& fontFamily, bool glass) {
    dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (glass && glassBitmap_) dc->DrawBitmap(glassBitmap_.Get());   // verre et ombres (Dock, infobulle)
    else drawBackground(dc, f, m);

    Com<ID2D1SolidColorBrush> sepBrush, dotBrush;
    dc->CreateSolidColorBrush(f.dark ? rgba(1, 1, 1, 0.25f) : rgba(0, 0, 0, 0.38f), &sepBrush);
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
            if (icon.dim > 0) {
                // Voile noir limité à la forme de l'icône (son alpha sert de masque).
                Com<ID2D1SolidColorBrush> veil;
                dc->CreateSolidColorBrush(rgba(0, 0, 0, icon.dim * icon.opacity), &veil);
                dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
                dc->FillOpacityMask(bmp, veil.Get(), dst, nullptr);
                dc->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            }
        }
        if (icon.indicator) {
            float r = float(m.indicatorDiameter) * f.scale / 2;
            dc->FillEllipse(D2D1::Ellipse(D2D1::Point2F(icon.cx, icon.indicatorY), r, r), dotBrush.Get());
        }
    }

    drawTooltip(dc, f, m, fontFamily, glass);
    drawOverlay(dc, f);
}

void DockRenderer::drawOverlay(ID2D1DeviceContext* dc, const RenderFrame& f) {
    if (!f.overlay || f.overlay->bgra.size() < size_t(f.overlay->w) * f.overlay->h * 4 || f.overlayOpacity <= 0) return;
    if (overlayOwner_.lock() != f.overlay || !overlayBitmap_) {
        overlayBitmap_.Reset();
        auto props = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE,
                                             D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(dc->CreateBitmap(D2D1::SizeU(f.overlay->w, f.overlay->h), f.overlay->bgra.data(), f.overlay->w * 4,
                                    props, &overlayBitmap_)))
            return;
        overlayOwner_ = f.overlay;
    }
    float w = f.overlay->w * f.overlayScale, h = f.overlay->h * f.overlayScale;
    float left = (float(width_) - w) / 2, bottom = float(height_);
    dc->DrawBitmap(overlayBitmap_.Get(), D2D1::RectF(left, bottom - h, left + w, bottom), f.overlayOpacity,
                   D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
}

std::vector<std::uint8_t> DockRenderer::renderOffscreen(const RenderFrame& f, const Metrics& m,
                                                        const std::wstring& fontFamily, UINT w, UINT h,
                                                        const std::vector<std::uint8_t>& wallpaper, float scRgbValue,
                                                        float sdrWhite) {
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

    const UINT savedW = width_, savedH = height_;
    const float savedWhite = sdrWhite_;
    width_ = w;
    height_ = h;

    // 1. Fond d'écran (image fournie, blanc pour un fond scRGB, sinon dégradé factice).
    dc_->SetTarget(target.Get());
    dc_->BeginDraw();
    dc_->SetDpi(96, 96);
    dc_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (wall) {
        dc_->DrawBitmap(wall.Get());
    } else if (scRgbValue >= 0) {
        dc_->Clear(rgba(1, 1, 1, 1));
    } else {
        D2D1_GRADIENT_STOP stops[3] = {{0, rgba(0.16f, 0.32f, 0.62f, 1)}, {0.5f, rgba(0.55f, 0.36f, 0.66f, 1)},
                                       {1, rgba(0.95f, 0.55f, 0.42f, 1)}};
        Com<ID2D1GradientStopCollection> coll;
        dc_->CreateGradientStopCollection(stops, 3, &coll);
        Com<ID2D1LinearGradientBrush> brush;
        dc_->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(float(w), float(h))), coll.Get(), &brush);
        dc_->FillRectangle(D2D1::RectF(0, 0, float(w), float(h)), brush.Get());
    }
    HRESULT hr = dc_->EndDraw();

    // 2. Arrière-plan du verre : copie du fond, ou fond scRGB uniforme (tests HDR).
    bool backdrop = false;
    Com<ID3D11DeviceContext> ctx;
    d3d_->GetImmediateContext(&ctx);
    if (SUCCEEDED(hr) && scRgbValue >= 0) {
        if (ID3D11Texture2D* tex = backdropTexture(w, h, true)) {
            std::vector<DirectX::PackedVector::HALF> px(size_t(w) * h * 4);
            const auto v = DirectX::PackedVector::XMConvertFloatToHalf(scRgbValue);
            const auto one = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
            for (size_t i = 0; i < px.size(); i += 4) px[i] = px[i + 1] = px[i + 2] = v, px[i + 3] = one;
            ctx->UpdateSubresource(tex, 0, nullptr, px.data(), w * 8, 0);
            sdrWhite_ = sdrWhite;
            backdrop = true;
        }
    } else if (SUCCEEDED(hr)) {
        Com<IDXGISurface> surface;
        Com<ID3D11Texture2D> targetTex;
        ID3D11Texture2D* tex = backdropTexture(w, h, false);
        if (tex && SUCCEEDED(target->GetSurface(&surface)) && SUCCEEDED(surface.As(&targetTex))) {
            ctx->CopyResource(tex, targetTex.Get());
            sdrWhite_ = 1;
            backdrop = true;
        }
    }

    // 3. Verre, puis le Dock par-dessus.
    const bool glass = backdrop && runGlass(f, m, fontFamily, w, h);
    if (SUCCEEDED(hr)) {
        dc_->BeginDraw();
        drawFrame(dc_.Get(), f, m, fontFamily, glass);
        hr = dc_->EndDraw();
    }
    dc_->SetTarget(nullptr);
    width_ = savedW;
    height_ = savedH;
    sdrWhite_ = savedWhite;
    if (FAILED(hr) || FAILED(readback->CopyFromBitmap(nullptr, target.Get(), nullptr))) return {};

    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped))) return {};
    std::vector<std::uint8_t> out(size_t(w) * h * 4);
    for (UINT y = 0; y < h; ++y) std::copy_n(mapped.bits + size_t(y) * mapped.pitch, size_t(w) * 4, &out[size_t(y) * w * 4]);
    readback->Unmap();
    return out;
}

std::vector<std::uint8_t> DockRenderer::renderToBgra(const RenderFrame& f, const Metrics& m,
                                                     const std::wstring& fontFamily, UINT w, UINT h,
                                                     const std::vector<std::uint8_t>& wallpaper) {
    return renderOffscreen(f, m, fontFamily, w, h, wallpaper, -1, 1);
}

std::vector<std::uint8_t> DockRenderer::renderToBgraScRgb(const RenderFrame& f, const Metrics& m,
                                                          const std::wstring& fontFamily, UINT w, UINT h, float value,
                                                          float sdrWhiteScale) {
    return renderOffscreen(f, m, fontFamily, w, h, {}, std::max(0.0f, value), sdrWhiteScale);
}

bool DockRenderer::renderToFile(const RenderFrame& f, const Metrics& m, const std::wstring& fontFamily, UINT w,
                                UINT h, const std::wstring& path, const std::vector<std::uint8_t>& wallpaper) {
    auto px = renderToBgra(f, m, fontFamily, w, h, wallpaper);
    return !px.empty() && writePng(path, px.data(), w, h);
}

} // namespace md
