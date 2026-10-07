#include "switcher_window.h"

#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <shellscalingapi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <map>

#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "../glass/glass_renderer.h"
#include "../popup/popup_glass.h"
#include "../theme/wallpaper_art.h"

#pragma comment(lib, "shcore.lib")   // GetDpiForMonitor (aussi dans la cible des tests)

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacDockSwitcher";
constexpr UINT WM_SW_BACKDROP = WM_APP + 41;
constexpr float kShadow = 24, kRadius = 18, kLabelFont = 13;

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

using IconDraw = std::function<void(ID2D1DeviceContext*, std::size_t index, const D2D1_RECT_F&)>;

// Contenu du panneau en points, origine au coin du panneau : sélection, icônes, nom de l'app sélectionnée.
void paintPanel(ID2D1DeviceContext* d, IDWriteFactory* dw, const std::wstring& font, const SwitcherGeometry& g,
                const std::vector<std::wstring>& names, std::size_t selected, bool dark, const IconDraw& icon) {
    Com<ID2D1SolidColorBrush> sel, ink;
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.18f) : rgba(0, 0, 0, 0.10f), &sel);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.92f) : rgba(0, 0, 0, 0.86f), &ink);
    if (!sel || !ink) return;
    const float pad = float(g.pad), cell = float(g.cell), ic = float(g.icon);
    for (std::size_t i = 0; i < names.size(); ++i) {
        const float x = pad + cell * float(i);
        if (i == selected) {
            const D2D1_RECT_F r{x + 2, pad + 2, x + cell - 2, pad + cell - 2};
            d->FillRoundedRectangle(D2D1::RoundedRect(r, cell * 0.16f, cell * 0.16f), sel.Get());
        }
        const float off = (cell - ic) / 2;
        if (icon) icon(d, i, D2D1::RectF(x + off, pad + off, x + off + ic, pad + off + ic));
    }
    if (selected >= names.size() || !dw) return;
    Com<IDWriteTextFormat> fmt;
    if (FAILED(dw->CreateTextFormat(font.empty() ? L"Segoe UI" : font.c_str(), nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
                                    DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, kLabelFont, L"", &fmt)))
        return;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    Com<IDWriteInlineObject> ellipsis;
    dw->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    fmt->SetTrimming(&trim, ellipsis.Get());
    const std::wstring& name = names[selected];
    Com<IDWriteTextLayout> l;
    const float w = float(g.width) - 2 * pad;
    if (FAILED(dw->CreateTextLayout(name.c_str(), UINT32(name.size()), fmt.Get(), std::max(1.0f, w), float(g.labelH), &l)))
        return;
    DWRITE_TEXT_METRICS tm{};
    l->GetMetrics(&tm);
    const float tw = std::min(tm.width, w);   // sous l'icône sélectionnée, sans sortir du panneau
    const float x = std::clamp(pad + cell * (float(selected) + 0.5f) - tw / 2, pad, pad + w - tw);
    d->DrawTextLayout({x, pad + cell + (float(g.labelH) - tm.height) / 2 - 2}, l.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

std::uint32_t placeholderColor(std::size_t i) {
    static const std::uint32_t colors[] = {0xFF0A84FF, 0xFF30D158, 0xFFFF9F0A, 0xFFFF375F, 0xFFBF5AF2, 0xFF64D2FF, 0xFFFFD60A};
    return colors[i % std::size(colors)];
}

} // namespace

struct SwitcherWindow::Impl {
    SwitcherWindow* owner = nullptr;
    MenuWindow::Env env;
    float sc = 1;
    std::vector<Entry> entries;
    std::size_t selected = 0;
    SwitcherGeometry g;
    bool shown = false;

    Com<ID3D11Device> device;   // celui d'env, gardé pour savoir s'il a changé
    Com<ID2D1Factory3> factory;
    Com<ID2D1Device2> d2d;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionSurface> surface;
    Com<IDWriteFactory3> dwrite;
    UINT surfW = 0, surfH = 0;

    GlassRenderer glass;
    bool glassReady = false;
    ScreenBackdrop screen;
    WindowBackdrop backdrop;
    GlassTarget glassTarget;

    HWND hwnd = nullptr;
    RECT rc{}, monitorRc{};
    std::map<std::size_t, Com<ID2D1Bitmap1>> bitmaps;

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }
    bool ensureDevice();
    bool ensureWindow();
    void layout();
    void render();
    std::vector<std::wstring> names() const {
        std::vector<std::wstring> n;
        for (const auto& e : entries) n.push_back(e.name);
        return n;
    }
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
};

bool SwitcherWindow::Impl::ensureDevice() {
    if (!env.device) return false;
    if (device.Get() == env.device && dc) return true;
    device = env.device;
    factory.Reset();
    d2d.Reset();
    dc.Reset();
    dcomp.Reset();
    target.Reset();
    visual.Reset();
    surface.Reset();
    surfW = surfH = 0;
    bitmaps.clear();
    Com<IDXGIDevice> dxgi;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(env.device->QueryInterface(IID_PPV_ARGS(&dxgi))) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDevice(dxgi.Get(), &d2d)) ||
        FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)) ||
        FAILED(DCompositionCreateDevice2(d2d.Get(), IID_PPV_ARGS(&dcomp)))) {
        dc.Reset();
        return false;
    }
    if (!dwrite)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    glassReady = glass.init(env.device);
    return true;
}

LRESULT CALLBACK SwitcherWindow::Impl::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'app au premier plan garde le clavier
        case WM_LBUTTONUP: {
            const double x = short(LOWORD(lp)) / self->sc - kShadow, y = short(HIWORD(lp)) / self->sc - kShadow;
            const int i = switcherHit(self->g, self->entries.size(), x, y);
            if (i >= 0 && self->owner->onClick) self->owner->onClick(std::size_t(i));
            return 0;
        }
        case WM_SW_BACKDROP:
            if (self->shown && self->screen.take(self->env.device) && self->screen.copyTo(self->env.device, self->rc, self->backdrop))
                self->render();
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool SwitcherWindow::Impl::ensureWindow() {
    if (hwnd) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClass,
                           L"Sélecteur d'apps", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, env.instance, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);   // sinon le verre se verrait lui-même
    return true;
}

// Taille et place du panneau (centré sur l'écran), surface à la taille de la fenêtre.
void SwitcherWindow::Impl::layout() {
    const double monW = double(monitorRc.right - monitorRc.left) / sc;
    g = switcherLayout(entries.size(), monW);
    const int w = int(std::lround((g.width + 2 * kShadow) * sc)), h = int(std::lround((g.height + 2 * kShadow) * sc));
    rc.left = (monitorRc.left + monitorRc.right) / 2 - w / 2;
    rc.top = (monitorRc.top + monitorRc.bottom) / 2 - h / 2;
    rc.right = rc.left + w;
    rc.bottom = rc.top + h;
    SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, w, h, SWP_NOACTIVATE);
    if (!surface || surfW != UINT(w) || surfH != UINT(h)) {
        surface.Reset();
        if (!target) {
            dcomp->CreateTargetForHwnd(hwnd, TRUE, &target);
            dcomp->CreateVisual(&visual);
            if (target && visual) target->SetRoot(visual.Get());
        }
        if (visual && SUCCEEDED(dcomp->CreateSurface(UINT(w), UINT(h), DXGI_FORMAT_B8G8R8A8_UNORM,
                                                     DXGI_ALPHA_MODE_PREMULTIPLIED, &surface))) {
            visual->SetContent(surface.Get());
            surfW = UINT(w);
            surfH = UINT(h);
        }
    }
    if (shown && backdrop.valid) screen.copyTo(env.device, rc, backdrop);   // la fenêtre a changé de taille
}

void SwitcherWindow::Impl::render() {
    if (!surface) return;
    const float m = kShadow * sc;
    const D2D1_RECT_F panel{m, m, m + float(g.width) * sc, m + float(g.height) * sc};
    const float radius = float(limitedCornerRadius(panel.right - panel.left, panel.bottom - panel.top, kRadius * sc));
    bool glassDrawn = false;
    if (env.glass && glassReady && backdrop.valid && glassTarget.ensure(env.device, dc.Get(), width(), height())) {
        const Metrics& mt = env.metrics;   // mêmes réglages que les menus
        GlassParams gp;
        gp.scale = sc;
        gp.dark = env.dark;
        gp.blurSigmaPx = float(mt.glassBlur) * 2.2f * sc;
        gp.bevelPx = float(mt.glassBevel) * 0.6f * sc;
        gp.refraction = float(mt.glassRefraction) * 0.5f;
        gp.chromatic = float(mt.glassChromatic) * 0.5f;
        gp.fresnel = float(mt.glassFresnel);
        gp.specular = float(mt.glassSpecular);
        gp.tint = std::min(1.0f, float(env.dark ? mt.glassTintDark : mt.glassTintLight) * 2.4f);
        gp.saturation = float(mt.glassSaturation);
        gp.shadowBlurPx = std::min(float(mt.shadowBlur), kShadow * 0.7f) * sc;
        gp.shadowOffsetPx = 3 * sc;
        gp.backdropIsScRgb = backdrop.scRgb;
        gp.sdrWhiteScale = backdrop.white;
        GlassShape shape{panel.left, panel.top, panel.right, panel.bottom, radius, 0.6f,
                         float(mt.shadowOpacity * (env.dark ? 2.0 : 1.4)), 1};
        Com<ID3D11DeviceContext> ctx;
        env.device->GetImmediateContext(&ctx);
        glassDrawn = glass.render(ctx.Get(), backdrop.srv.Get(), width(), height(), glassTarget.rtv.Get(), {&shape, 1}, gp);
    }
    POINT offset{};
    Com<ID2D1DeviceContext> d;
    if (FAILED(surface->BeginDraw(nullptr, IID_PPV_ARGS(&d), &offset))) return;
    d->SetDpi(96, 96);
    const auto base = D2D1::Matrix3x2F::Translation(float(offset.x), float(offset.y));
    d->SetTransform(base);
    d->Clear(rgba(0, 0, 0, 0));
    d->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const D2D1_ROUNDED_RECT rr{panel, radius, radius};
    if (glassDrawn) {
        d->DrawBitmap(glassTarget.bitmap.Get());
    } else {   // verre dépoli de repli, comme les menus
        Com<ID2D1SolidColorBrush> bg;
        d->CreateSolidColorBrush(env.dark ? rgba(0.15f, 0.15f, 0.16f, 0.94f) : rgba(0.96f, 0.96f, 0.97f, 0.94f), &bg);
        if (bg) d->FillRoundedRectangle(rr, bg.Get());
    }
    Com<ID2D1SolidColorBrush> border;
    d->CreateSolidColorBrush(env.dark ? rgba(1, 1, 1, 0.14f) : rgba(0, 0, 0, 0.10f), &border);
    if (border) d->DrawRoundedRectangle(rr, border.Get(), std::max(1.0f, sc * 0.5f));
    d->SetTransform(D2D1::Matrix3x2F::Scale(sc, sc) * D2D1::Matrix3x2F::Translation(m, m) * base);
    paintPanel(d.Get(), dwrite.Get(), env.font, g, names(), selected, env.dark,
               [this](ID2D1DeviceContext* dd, std::size_t i, const D2D1_RECT_F& r) {
                   if (i >= entries.size() || !entries[i].icon) return;
                   auto& b = bitmaps[i];
                   if (!b) {
                       const auto& im = *entries[i].icon;
                       auto props = D2D1::BitmapProperties1(
                           D2D1_BITMAP_OPTIONS_NONE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
                       dc->CreateBitmap(D2D1::SizeU(UINT32(im.size), UINT32(im.size)), im.bgra.data(), UINT32(im.size * 4),
                                        &props, &b);
                   }
                   if (b) dd->DrawBitmap(b.Get(), r, 1, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC);
               });
    surface->EndDraw();
    dcomp->Commit();
}

SwitcherWindow::SwitcherWindow() : impl_(std::make_unique<Impl>()) { impl_->owner = this; }

SwitcherWindow::~SwitcherWindow() {
    hide();
    if (impl_->hwnd) DestroyWindow(impl_->hwnd);
}

bool SwitcherWindow::show(const MenuWindow::Env& env, HMONITOR mon, std::vector<Entry> entries, std::size_t selected) {
    Impl& s = *impl_;
    if (entries.empty()) return false;
    s.env = env;
    if (!s.ensureDevice() || !s.ensureWindow()) {
        log::warn(L"Sélecteur d'apps : panneau impossible");
        return false;
    }
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    s.monitorRc = mi.rcMonitor;
    UINT dx = 96, dy = 96;
    s.sc = SUCCEEDED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dx, &dy)) ? float(dx) / 96.0f : env.scale;
    s.entries = std::move(entries);
    s.selected = std::min(selected, s.entries.size() - 1);
    s.bitmaps.clear();
    s.layout();
    if (!s.surface) return false;
    s.backdrop.valid = false;
    if (env.glass && s.glassReady) s.screen.start(s.hwnd, WM_SW_BACKDROP, mon, mi.rcMonitor);
    s.shown = true;
    s.render();
    ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void SwitcherWindow::select(std::size_t index) {
    Impl& s = *impl_;
    if (!s.shown || index >= s.entries.size() || index == s.selected) return;
    s.selected = index;
    s.render();
}

void SwitcherWindow::remove(std::size_t index) {
    Impl& s = *impl_;
    if (!s.shown || index >= s.entries.size()) return;
    s.entries.erase(s.entries.begin() + std::ptrdiff_t(index));
    if (s.entries.empty()) {
        hide();
        return;
    }
    s.bitmaps.clear();
    if (s.selected >= s.entries.size()) s.selected = s.entries.size() - 1;
    s.layout();
    s.render();
}

void SwitcherWindow::hide() {
    Impl& s = *impl_;
    s.screen.stop();
    s.shown = false;
    s.backdrop.valid = false;
    if (s.hwnd) ShowWindow(s.hwnd, SW_HIDE);
    MSG msg;
    while (s.hwnd && PeekMessageW(&msg, s.hwnd, WM_SW_BACKDROP, WM_SW_BACKDROP, PM_REMOVE)) {
    }
}

bool SwitcherWindow::visible() const { return impl_->shown; }

BgraImage switcherSnapshot(const std::vector<std::wstring>& names, std::size_t selected, bool dark, int width, int height) {
    BgraImage out{width, height, std::vector<std::uint8_t>(std::size_t(std::max(width, 0)) * std::max(height, 0) * 4, 0)};
    if (width <= 0 || height <= 0) return out;
    Com<ID3D11Device> dev;
    const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, nullptr)) &&
        FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                 nullptr, nullptr)))
        return out;
    Com<IDXGIDevice> dxgi;
    Com<ID2D1Factory3> factory;
    Com<ID2D1Device2> d2d;
    Com<ID2D1DeviceContext2> dc;
    D2D1_FACTORY_OPTIONS opts{};
    if (FAILED(dev.As(&dxgi)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory3), &opts,
                                 reinterpret_cast<void**>(factory.GetAddressOf()))) ||
        FAILED(factory->CreateDevice(dxgi.Get(), &d2d)) ||
        FAILED(d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc)))
        return out;
    Com<IDWriteFactory3> dwrite;
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory3), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    const auto fmt = D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    Com<ID2D1Bitmap1> target, readback, wallBmp;
    const auto size = D2D1::SizeU(UINT32(width), UINT32(height));
    if (FAILED(dc->CreateBitmap(size, nullptr, 0, D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET, fmt), &target)) ||
        FAILED(dc->CreateBitmap(size, nullptr, 0,
                                D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW, fmt),
                                &readback)))
        return out;
    const BgraImage wall = macWallpaper(width, height, dark);
    dc->CreateBitmap(size, wall.px.data(), UINT32(width * 4), D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_NONE, fmt), &wallBmp);
    Com<ID2D1Effect> blur;
    if (wallBmp && SUCCEEDED(dc->CreateEffect(CLSID_D2D1GaussianBlur, &blur))) {
        blur->SetInput(0, wallBmp.Get());
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 24.0f);
        blur->SetValue(D2D1_GAUSSIANBLUR_PROP_BORDER_MODE, D2D1_BORDER_MODE_HARD);
    }

    dc->SetTarget(target.Get());
    dc->BeginDraw();
    dc->Clear(rgba(0, 0, 0, 1));
    if (wallBmp) dc->DrawBitmap(wallBmp.Get());
    if (!names.empty()) {
        const SwitcherGeometry g = switcherLayout(names.size(), width);
        const float left = std::floor((float(width) - float(g.width)) / 2), top = std::floor((float(height) - float(g.height)) / 2);
        const D2D1_RECT_F panel{left, top, left + float(g.width), top + float(g.height)};
        const D2D1_ROUNDED_RECT rr{panel, kRadius, kRadius};
        Com<ID2D1RoundedRectangleGeometry> clip;
        factory->CreateRoundedRectangleGeometry(rr, &clip);
        if (blur && clip) {   // verre dépoli : le fond flouté dans le panneau
            dc->PushLayer(D2D1::LayerParameters1(D2D1::InfiniteRect(), clip.Get()), nullptr);
            dc->DrawImage(blur.Get());
            dc->PopLayer();
        }
        Com<ID2D1SolidColorBrush> tint, border;
        dc->CreateSolidColorBrush(dark ? rgba(0.12f, 0.12f, 0.13f, 0.62f) : rgba(1, 1, 1, 0.62f), &tint);
        if (tint) dc->FillRoundedRectangle(rr, tint.Get());
        dc->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.14f) : rgba(0, 0, 0, 0.10f), &border);
        if (border) dc->DrawRoundedRectangle(rr, border.Get(), 1);
        dc->SetTransform(D2D1::Matrix3x2F::Translation(left, top));
        paintPanel(dc.Get(), dwrite.Get(), L"", g, names, selected, dark,
                   [](ID2D1DeviceContext* d, std::size_t i, const D2D1_RECT_F& r) {
                       Com<ID2D1SolidColorBrush> b;
                       const std::uint32_t c = placeholderColor(i);
                       d->CreateSolidColorBrush(
                           rgba(float((c >> 16) & 255) / 255, float((c >> 8) & 255) / 255, float(c & 255) / 255, 1), &b);
                       const float k = (r.right - r.left) * 0.225f;
                       if (b) d->FillRoundedRectangle(D2D1::RoundedRect(r, k, k), b.Get());
                   });
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
    }
    if (FAILED(dc->EndDraw())) return out;
    D2D1_POINT_2U origin{0, 0};
    D2D1_RECT_U all{0, 0, UINT32(width), UINT32(height)};
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readback->CopyFromBitmap(&origin, target.Get(), &all)) || FAILED(readback->Map(D2D1_MAP_OPTIONS_READ, &mapped)))
        return out;
    for (int y = 0; y < height; ++y)
        std::copy_n(mapped.bits + std::size_t(y) * mapped.pitch, std::size_t(width) * 4, &out.px[std::size_t(y) * width * 4]);
    readback->Unmap();
    return out;
}

} // namespace md
