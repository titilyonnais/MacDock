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

// Distance du haut de la mise en page à la ligne de base de sa première ligne (un titre peut contenir un saut de
// ligne : sa première ligne se pose sur la ligne de base de la barre).
float firstBaseline(IDWriteTextLayout* l) {
    DWRITE_LINE_METRICS lm{};
    UINT32 lines = 0;
    if (SUCCEEDED(l->GetLineMetrics(&lm, 1, &lines))) return lm.baseline;
    if (lines == 0) return 0;
    std::vector<DWRITE_LINE_METRICS> all(lines);
    return SUCCEEDED(l->GetLineMetrics(all.data(), lines, &lines)) && lines > 0 ? all[0].baseline : 0;
}

// Hauteur des majuscules de la police du début de la mise en page, en DIP (0 si introuvable).
float capHeightOf(IDWriteTextLayout* l, float fontSize) {
    Microsoft::WRL::ComPtr<IDWriteFontCollection> fonts;
    if (FAILED(l->GetFontCollection(0, &fonts, nullptr)) || !fonts) return 0;
    wchar_t family[128] = {};
    if (FAILED(l->GetFontFamilyName(0, family, 128, nullptr))) return 0;
    DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_NORMAL;
    l->GetFontWeight(0, &weight, nullptr);
    UINT32 index = 0;
    BOOL exists = FALSE;
    Microsoft::WRL::ComPtr<IDWriteFontFamily> fam;
    Microsoft::WRL::ComPtr<IDWriteFont> font;
    if (FAILED(fonts->FindFamilyName(family, &index, &exists)) || !exists || FAILED(fonts->GetFontFamily(index, &fam)) ||
        FAILED(fam->GetFirstMatchingFont(weight, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &font)))
        return 0;
    DWRITE_FONT_METRICS m{};
    font->GetMetrics(&m);
    return m.designUnitsPerEm ? fontSize * m.capHeight / m.designUnitsPerEm : 0;
}

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

Microsoft::WRL::ComPtr<IDWriteTextLayout> BarRenderer::layoutOf(const std::wstring& text, bool bold, bool tabular) {
    Com<IDWriteTextLayout> l;
    IDWriteTextFormat* fmt = bold ? bold_.Get() : regular_.Get();
    if (fmt) dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt, 10000, 1000, &l);
    if (l && tabular) {
        if (!tabular_ && SUCCEEDED(dwrite_->CreateTypography(&tabular_)))
            tabular_->AddFontFeature({DWRITE_FONT_FEATURE_TAG_TABULAR_FIGURES, 1});
        if (tabular_) l->SetTypography(tabular_.Get(), DWRITE_TEXT_RANGE{0, UINT32(text.size())});
    }
    return l;
}

float BarRenderer::measure(const std::wstring& text, bool bold, bool tabular) {
    auto l = layoutOf(text, bold, tabular);
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
    const float o = std::clamp(f.opacity, 0.0f, 1.0f);
    rt->CreateSolidColorBrush(f.darkText ? rgba(0, 0, 0, 0.85f * o) : rgba(1, 1, 1, o), &ink);
    rt->CreateSolidColorBrush(rgba(0, 0, 0, 0.25f * o), &shadow);
    rt->CreateSolidColorBrush(f.darkText ? rgba(0, 0, 0, 0.10f * o) : rgba(1, 1, 1, 0.22f * o), &capsule);
    const float shadowDy = std::max(1.0f, std::round(s));
    const bool withShadow = !f.darkText;
    // Ligne de base des textes : textBaseline (barre de 24, texte de 13) au même écart du milieu de la barre, écart à la
    // mesure de la taille du texte, sur un pixel entier (Direct2D y cale le texte).
    const MenuBarMetrics ref;
    const float baseline =
        std::round(height / 2 + float((m.textBaseline - ref.height / 2) * m.fontSize / ref.fontSize) * s);

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
                rt->DrawBitmap(bmp.Get(), D2D1::RectF(x0, y0, x0 + size, y0 + size), o,
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
        if (it.boxed) {   // menu de saisie : le code de la disposition dans une boîte arrondie, comme les sources de macOS
            auto l = layoutOf(it.text, true);
            if (!l) continue;
            const float fs = bold_ ? bold_->GetFontSize() * 0.72f : 10 * s;
            l->SetFontSize(fs, DWRITE_TEXT_RANGE{0, UINT32(it.text.size())});
            DWRITE_TEXT_METRICS tm{};
            l->GetMetrics(&tm);
            const float bh = std::round(15 * s), bw = std::max(std::round(tm.widthIncludingTrailingWhitespace + 8 * s), std::round(20 * s));
            const float bx = std::round(it.x + (it.width - bw) / 2), by = std::round((height - bh) / 2);
            const float stroke = std::max(1.0f, 1.2f * s), r = 3.5f * s;
            // Majuscules au milieu de la boîte (la boîte de ligne dépend des marges verticales de chaque police : Segoe UI
            // 5.72, arrivée avec une mise à jour de Windows, les posait 1,5 px trop bas).
            const float cap = capHeightOf(l.Get(), fs);
            const float tx = std::round(bx + (bw - tm.widthIncludingTrailingWhitespace) / 2);
            const float ty = cap > 0 ? std::round(by + bh / 2 - firstBaseline(l.Get()) + cap / 2) : std::round(by + (bh - tm.height) / 2);
            for (int pass = withShadow ? 0 : 1; pass < 2; ++pass) {
                const float dy = pass == 0 ? shadowDy : 0;
                ID2D1SolidColorBrush* b = pass == 0 ? shadow.Get() : ink.Get();
                rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(bx + stroke / 2, by + dy + stroke / 2, bx + bw - stroke / 2,
                                                                       by + dy + bh - stroke / 2),
                                                           r, r),
                                         b, stroke);
                rt->DrawTextLayout({tx, ty + dy}, l.Get(), b);
            }
            continue;
        }
        auto l = layoutOf(it.text, it.bold, it.tabular);
        if (!l) continue;
        DWRITE_TEXT_METRICS tm{};
        l->GetMetrics(&tm);
        const float x = std::round(it.x + (it.width - tm.widthIncludingTrailingWhitespace) / 2);
        const float y = baseline - firstBaseline(l.Get());
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
