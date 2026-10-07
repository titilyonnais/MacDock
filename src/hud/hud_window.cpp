#include "hud_window.h"

#include <d2d1_3.h>
#include <d2d1effects.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "../core/log.h"
#include "../geom/smooth_rect.h"
#include "../glass/glass_renderer.h"
#include "../popup/glyphs.h"
#include "../popup/popup_glass.h"
#include "../theme/wallpaper_art.h"

namespace md {

namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr wchar_t kClass[] = L"MacMenuBarHud";
constexpr UINT WM_HUD_BACKDROP = WM_APP + 42;
constexpr float kW = 280, kH = 64, kRadius = 20, kShadow = 20;   // points
constexpr float kPad = 14, kTitleTop = 10, kTitleH = 18, kRowY = 45, kIcon = 16, kGauge = 6;

D2D1_COLOR_F rgba(float r, float g, float b, float a) { return D2D1::ColorF(r, g, b, a); }

Com<IDWriteTextLayout> textLayout(IDWriteFactory* dw, const std::wstring& font, const std::wstring& text, float size,
                                  DWRITE_FONT_WEIGHT weight, float maxW) {
    Com<IDWriteTextFormat> fmt;
    Com<IDWriteTextLayout> l;
    if (!dw || text.empty() ||
        FAILED(dw->CreateTextFormat(font.empty() ? L"Segoe UI" : font.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                    DWRITE_FONT_STRETCH_NORMAL, size, L"", &fmt)))
        return l;
    fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    Com<IDWriteInlineObject> ellipsis;
    dw->CreateEllipsisTrimmingSign(fmt.Get(), &ellipsis);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    fmt->SetTrimming(&trim, ellipsis.Get());
    dw->CreateTextLayout(text.c_str(), UINT32(text.size()), fmt.Get(), std::max(1.0f, maxW), kTitleH, &l);
    return l;
}

// Contenu de la pastille en points, origine au coin du panneau : titre, sortie à droite, pictogramme, jauge.
void paintHud(ID2D1DeviceContext* d, IDWriteFactory* dw, const std::wstring& font, const HudContent& c, bool dark) {
    Com<ID2D1SolidColorBrush> ink, dim, track, fill;
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.92f) : rgba(0, 0, 0, 0.86f), &ink);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.55f) : rgba(0, 0, 0, 0.50f), &dim);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.18f) : rgba(0, 0, 0, 0.12f), &track);
    d->CreateSolidColorBrush(dark ? rgba(1, 1, 1, 0.92f) : rgba(0, 0, 0, 0.72f), &fill);
    if (!ink || !dim || !track || !fill) return;
    const bool volume = c.kind == HudKind::Volume;
    const std::wstring title = volume ? L"Volume" : L"Luminosité";
    float titleW = 0;
    if (auto l = textLayout(dw, font, title, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, kW - 2 * kPad)) {
        DWRITE_TEXT_METRICS tm{};
        l->GetMetrics(&tm);
        titleW = tm.width;
        d->DrawTextLayout({kPad, kTitleTop}, l.Get(), ink.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    const float detailMax = kW - 2 * kPad - titleW - 10;
    if (volume && detailMax > 20)
        if (auto l = textLayout(dw, font, c.detail, 12, DWRITE_FONT_WEIGHT_NORMAL, detailMax)) {
            DWRITE_TEXT_METRICS tm{};
            l->GetMetrics(&tm);
            const float w = std::min(tm.width, detailMax);
            d->DrawTextLayout({kW - kPad - w, kTitleTop + 1}, l.Get(), dim.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        }
    const float level = std::clamp(c.muted ? 0.0f : c.level, 0.0f, 1.0f);
    drawGlyph(d, volume ? Glyph::Speaker : Glyph::Sun,
              D2D1::RectF(kPad, kRowY - kIcon / 2, kPad + kIcon, kRowY + kIcon / 2), ink.Get(), volume ? level : 1,
              volume && c.muted);
    const float gx = kPad + kIcon + 10, gw = kW - kPad - gx;
    const D2D1_RECT_F bar{gx, kRowY - kGauge / 2, gx + gw, kRowY + kGauge / 2};
    d->FillRoundedRectangle(D2D1::RoundedRect(bar, kGauge / 2, kGauge / 2), track.Get());
    if (level > 0) {
        const D2D1_RECT_F on{gx, bar.top, gx + std::max(kGauge, gw * level), bar.bottom};
        d->FillRoundedRectangle(D2D1::RoundedRect(on, kGauge / 2, kGauge / 2), fill.Get());
    }
}

} // namespace

struct HudWindow::Impl {
    MenuWindow::Env env;
    HudContent content;
    float sc = 1;
    bool shown = false;
    HMONITOR monitor = nullptr;

    Com<ID3D11Device> device;   // celui d'env, gardé pour savoir s'il a changé
    Com<ID2D1Factory3> factory;
    Com<ID2D1Device2> d2d;
    Com<ID2D1DeviceContext2> dc;
    Com<IDCompositionDesktopDevice> dcomp;
    Com<IDCompositionTarget> target;
    Com<IDCompositionVisual2> visual;
    Com<IDCompositionEffectGroup> fade;
    Com<IDCompositionSurface> surface;
    Com<IDWriteFactory3> dwrite;
    UINT surfW = 0, surfH = 0;

    GlassRenderer glass;
    bool glassReady = false;
    ScreenBackdrop screen;
    WindowBackdrop backdrop;
    GlassTarget glassTarget;

    HWND hwnd = nullptr;
    RECT rc{};

    UINT width() const { return UINT(rc.right - rc.left); }
    UINT height() const { return UINT(rc.bottom - rc.top); }
    bool ensureDevice();
    bool ensureWindow();
    bool place(const HudPlace& p);
    void render();
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
};

bool HudWindow::Impl::ensureDevice() {
    if (!env.device) return false;
    if (device.Get() == env.device && dc) return true;
    device = env.device;
    factory.Reset();
    d2d.Reset();
    dc.Reset();
    dcomp.Reset();
    target.Reset();
    visual.Reset();
    fade.Reset();
    surface.Reset();
    surfW = surfH = 0;
    glassTarget = GlassTarget{};
    backdrop = WindowBackdrop{};
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

LRESULT CALLBACK HudWindow::Impl::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'app au premier plan garde le clavier
        case WM_HUD_BACKDROP:
            if (self->shown && self->screen.take(self->env.device) && self->screen.copyTo(self->env.device, self->rc, self->backdrop))
                self->render();
            return 0;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

bool HudWindow::Impl::ensureWindow() {
    if (hwnd) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = env.instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kClass,
                           L"Volume", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, env.instance, nullptr);
    if (!hwnd) return false;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(this));
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);   // sinon le verre se verrait lui-même
    return true;
}

// Fenêtre = pastille + marge d'ombre ; surface à la taille de la fenêtre. false : composition impossible.
bool HudWindow::Impl::place(const HudPlace& p) {
    const int m = int(std::lround(kShadow * sc));
    rc = {p.x - m, p.y - m, p.x + p.w + m, p.y + p.h + m};
    SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, int(width()), int(height()), SWP_NOACTIVATE);
    if (!target) {
        dcomp->CreateTargetForHwnd(hwnd, TRUE, &target);
        dcomp->CreateVisual(&visual);
        dcomp->CreateEffectGroup(&fade);
        if (!target || !visual) return false;
        target->SetRoot(visual.Get());
        if (fade) visual->SetEffect(fade.Get());
    }
    if (!surface || surfW != width() || surfH != height()) {
        surface.Reset();
        if (FAILED(dcomp->CreateSurface(width(), height(), DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_ALPHA_MODE_PREMULTIPLIED, &surface)))
            return false;
        visual->SetContent(surface.Get());
        surfW = width();
        surfH = height();
    }
    return true;
}

void HudWindow::Impl::render() {
    if (!surface) return;
    const float m = kShadow * sc;
    const D2D1_RECT_F panel{m, m, m + kW * sc, m + kH * sc};
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
    paintHud(d.Get(), dwrite.Get(), env.font, content, env.dark);
    surface->EndDraw();
    dcomp->Commit();
}

HudWindow::HudWindow() : impl_(std::make_unique<Impl>()) {}

HudWindow::~HudWindow() {
    hide();
    if (impl_->hwnd) DestroyWindow(impl_->hwnd);
}

bool HudWindow::show(const MenuWindow::Env& env, HMONITOR mon, const HudPlace& place, const HudContent& content) {
    Impl& s = *impl_;
    const bool moved = !s.shown || mon != s.monitor || s.env.device != env.device || s.rc.left + int(std::lround(kShadow * s.sc)) != place.x ||
                       s.rc.top + int(std::lround(kShadow * s.sc)) != place.y;
    s.env = env;
    s.content = content;
    if (!s.ensureDevice() || !s.ensureWindow()) {
        log::warn(L"HUD : pastille impossible");
        return false;
    }
    if (moved) {
        s.sc = std::max(0.5f, float(place.w) / kW);
        if (s.shown) hide();
        if (!s.place(place)) return false;
        MONITORINFO mi{sizeof mi};
        s.monitor = mon;
        s.backdrop.valid = false;
        if (env.glass && s.glassReady && GetMonitorInfoW(mon, &mi)) s.screen.start(s.hwnd, WM_HUD_BACKDROP, mon, mi.rcMonitor);
    }
    s.shown = true;
    setOpacity(1);
    s.render();
    if (moved) ShowWindow(s.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void HudWindow::setOpacity(float opacity) {
    Impl& s = *impl_;
    if (!s.fade || !s.dcomp) return;
    s.fade->SetOpacity(std::clamp(opacity, 0.0f, 1.0f));
    s.dcomp->Commit();
}

void HudWindow::hide() {
    Impl& s = *impl_;
    s.screen.stop();
    s.shown = false;
    s.backdrop.valid = false;
    if (s.hwnd) ShowWindow(s.hwnd, SW_HIDE);
    MSG msg;
    while (s.hwnd && PeekMessageW(&msg, s.hwnd, WM_HUD_BACKDROP, WM_HUD_BACKDROP, PM_REMOVE)) {
    }
}

bool HudWindow::visible() const { return impl_->shown; }

BgraImage hudSnapshot(const HudContent& content, bool dark, int width, int height) {
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
    const BgraImage wall = tahoeWallpaper(width, height, dark);
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
    const HudPlace p = hudPlace({0, 0, width, height}, 24, 1);
    const D2D1_RECT_F panel{float(p.x), float(p.y), float(p.x + p.w), float(p.y + p.h)};
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
    dc->SetTransform(D2D1::Matrix3x2F::Translation(panel.left, panel.top));
    paintHud(dc.Get(), dwrite.Get(), L"", content, dark);
    dc->SetTransform(D2D1::Matrix3x2F::Identity());
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
