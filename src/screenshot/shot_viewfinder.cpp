#include "shot_viewfinder.h"

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "../core/diag.h"
#include "../core/log.h"
#include "screen_grab.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {

constexpr wchar_t kCatcherClass[] = L"MacDockShotCatcher";
constexpr wchar_t kTintClass[] = L"MacDockShotTint";

BOOL CALLBACK addMonitor(HMONITOR m, HDC, LPRECT, LPARAM lp) {
    reinterpret_cast<std::vector<HMONITOR>*>(lp)->push_back(m);
    return TRUE;
}

// Curseur appareil photo dessiné par le code (point chaud au centre).
HCURSOR makeCameraCursor() {
    const int size = GetSystemMetrics(SM_CXCURSOR) > 0 ? GetSystemMetrics(SM_CXCURSOR) : 32;
    const std::vector<std::uint8_t> px = cameraCursorPixels(size);
    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof bi;
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!color) return nullptr;
    std::memcpy(bits, px.data(), px.size());
    const std::vector<BYTE> zeros(std::size_t((size + 15) / 16 * 2) * size, 0);   // lignes alignées sur 16 bits
    HBITMAP mask = CreateBitmap(size, size, 1, 1, zeros.data());
    ICONINFO ii{FALSE, DWORD(size / 2), DWORD(size / 2), mask, color};
    HCURSOR c = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return c;
}

} // namespace

// ---- Voile ----

bool ShotViewfinder::Tint::create(HINSTANCE instance, COLORREF fillColor, COLORREF borderColor, BYTE alpha) {
    if (hwnd) return true;
    fill = fillColor;
    border = borderColor;
    hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kTintClass, L"",
                           WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, this);
    if (!hwnd) return false;
    SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    if (!diagnosticCapture()) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    return true;
}

void ShotViewfinder::Tint::show(const RECT& r, int corner) {
    if (!hwnd) return;
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (corner != radius || w != shown.right - shown.left || h != shown.bottom - shown.top) {
        SetWindowRgn(hwnd, corner > 0 ? CreateRoundRectRgn(0, 0, w + 1, h + 1, 2 * corner, 2 * corner) : nullptr, TRUE);
        radius = corner;
    }
    shown = r;
    SetWindowPos(hwnd, HWND_TOPMOST, r.left, r.top, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(hwnd, nullptr, FALSE);
}

void ShotViewfinder::Tint::hide() {
    if (hwnd) ShowWindow(hwnd, SW_HIDE);
    shown = {};
}

void ShotViewfinder::Tint::destroy() {
    if (hwnd) DestroyWindow(hwnd);
    hwnd = nullptr;
    shown = {};
    radius = -1;
}

LRESULT CALLBACK ShotViewfinder::tintProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
            break;
        case WM_NCDESTROY:
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            const auto* c = reinterpret_cast<const Tint*>(GetWindowLongPtrW(h, GWLP_USERDATA));
            RECT rc;
            GetClientRect(h, &rc);
            if (c) {
                HBRUSH fill = CreateSolidBrush(c->fill);
                FillRect(dc, &rc, fill);
                DeleteObject(fill);
                if (c->border != c->fill) {   // liseré clair d'un pixel (la sélection de macOS)
                    HBRUSH border = CreateSolidBrush(c->border);
                    FrameRect(dc, &rc, border);
                    DeleteObject(border);
                }
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_NCHITTEST: return HTTRANSPARENT;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ---- Viseur ----

ShotViewfinder::~ShotViewfinder() {
    teardown();
    if (camera_) DestroyCursor(camera_);
}

bool ShotViewfinder::start(HINSTANCE instance, Done done, std::function<bool(HWND)> ignore) {
    if (active_) return false;
    instance_ = instance;
    done_ = std::move(done);
    ignore_ = std::move(ignore);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = catcherProc;
    wc.hInstance = instance;
    wc.lpszClassName = kCatcherClass;
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    RegisterClassExW(&wc);
    WNDCLASSEXW tc{sizeof tc};
    tc.lpfnWndProc = tintProc;
    tc.hInstance = instance;
    tc.lpszClassName = kTintClass;
    RegisterClassExW(&tc);
    if (!cross_) cross_ = LoadCursorW(nullptr, IDC_CROSS);
    if (!camera_) camera_ = makeCameraCursor();
    if (!d2d_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (!dwrite_)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    if (!wic_) CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_));
    // Une fenêtre de saisie par écran : presque invisible (alpha 1/255) mais elle reçoit la souris.
    std::vector<HMONITOR> monitors;
    EnumDisplayMonitors(nullptr, nullptr, addMonitor, reinterpret_cast<LPARAM>(&monitors));
    for (HMONITOR m : monitors) {
        MONITORINFO mi{sizeof mi};
        if (!GetMonitorInfoW(m, &mi)) continue;
        const RECT& r = mi.rcMonitor;
        HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kCatcherClass, L"", WS_POPUP,
                                 r.left, r.top, r.right - r.left, r.bottom - r.top, nullptr, nullptr, instance, this);
        if (!h) continue;
        SetLayeredWindowAttributes(h, 0, 1, LWA_ALPHA);
        if (!diagnosticCapture()) SetWindowDisplayAffinity(h, WDA_EXCLUDEFROMCAPTURE);
        ShowWindow(h, SW_SHOWNOACTIVATE);
        catchers_.push_back(h);
    }
    if (catchers_.empty()) {
        log::warn(L"Capture d'écran : viseur impossible (%lu)", GetLastError());
        return false;
    }
    selection_.create(instance, RGB(120, 120, 120), RGB(255, 255, 255), 90);   // gris translucide, liseré clair
    highlight_.create(instance, RGB(96, 156, 255), RGB(96, 156, 255), 80);     // voile bleu du mode fenêtre
    label_.create(instance);
    active_ = true;
    windowMode_ = dragging_ = false;
    hovered_ = nullptr;
    POINT pt;
    GetCursorPos(&pt);
    SetCursor(cross_);
    onMove(pt);
    return true;
}

void ShotViewfinder::teardown() {
    for (HWND h : catchers_) DestroyWindow(h);
    catchers_.clear();
    selection_.destroy();
    highlight_.destroy();
    label_.hide();
    active_ = dragging_ = windowMode_ = false;
    rightPressed_ = windowPressed_ = false;
    hovered_ = nullptr;
}

void ShotViewfinder::finish(const Result& r) {
    if (!active_) return;
    if (GetCapture() && std::find(catchers_.begin(), catchers_.end(), GetCapture()) != catchers_.end()) ReleaseCapture();
    teardown();
    Done done = std::move(done_);
    done_ = nullptr;
    if (done) done(r);
}

void ShotViewfinder::cancel() { finish(Result{}); }

void ShotViewfinder::key(ShotSessionKey k) {
    if (!active_) return;
    if (k == ShotSessionKey::Cancel) {
        cancel();
        return;
    }
    if (k != ShotSessionKey::ToggleWindow || dragging_) return;   // macOS : Espace pendant le tirer déplace la zone
    windowMode_ = !windowMode_;
    POINT pt;
    GetCursorPos(&pt);
    SetCursor(windowMode_ ? (camera_ ? camera_ : cross_) : cross_);
    if (windowMode_) {
        label_.hide();
        hover(pt);
    } else {
        highlight_.hide();
        hovered_ = nullptr;
        onMove(pt);
    }
}

double ShotViewfinder::scaleAt(POINT pt) const { return monitorScale(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST)); }

void ShotViewfinder::hover(POINT pt) {
    HWND w = topWindowAt(pt, ignore_);
    if (w == hovered_) return;
    hovered_ = w;
    if (!w) {
        highlight_.hide();
        return;
    }
    highlight_.show(windowFrameBounds(w), windowCornerRadius(w));
}

void ShotViewfinder::onMove(POINT pt) {
    if (windowMode_) {
        hover(pt);
        return;
    }
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(dragging_ ? anchor_ : pt, MONITOR_DEFAULTTONEAREST), &mi);
    const double scale = scaleAt(dragging_ ? anchor_ : pt);
    if (dragging_) {
        const RECT r = selectionRect(anchor_, pt, bounds_);
        if (r.right > r.left && r.bottom > r.top) selection_.show(r, 0);
        else selection_.hide();
        showLabel(pt, int(std::lround((r.right - r.left) / scale)), int(std::lround((r.bottom - r.top) / scale)));
    } else {
        showLabel(pt, int(std::lround((pt.x - mi.rcMonitor.left) / scale)), int(std::lround((pt.y - mi.rcMonitor.top) / scale)));
    }
}

void ShotViewfinder::showLabel(POINT pt, int a, int b) {
    if (!d2d_ || !dwrite_ || !wic_) return;
    const float scale = float(scaleAt(pt));
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwrite_->CreateTextFormat(L"SF Pro Text", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, 11.0f * scale, L"fr-fr", &format)))
        return;
    const std::wstring text = std::to_wstring(a) + L"\n" + std::to_wstring(b);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextLayout(text.c_str(), UINT32(text.size()), format.Get(), 400, 200, &layout))) return;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    const float pad = 2 * scale;
    const UINT w = UINT(std::ceil(m.widthIncludingTrailingWhitespace + 2 * pad)), h = UINT(std::ceil(m.height + 2 * pad));
    ComPtr<IWICBitmap> bitmap;
    ComPtr<ID2D1RenderTarget> rt;
    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_SOFTWARE, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96, 96);
    if (FAILED(wic_->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bitmap)) ||
        FAILED(d2d_->CreateWicBitmapRenderTarget(bitmap.Get(), props, &rt)))
        return;
    ComPtr<ID2D1SolidColorBrush> halo, ink;
    rt->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 0.9f), &halo);
    rt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 0.85f), &ink);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    // Halo blanc autour du texte noir : lisible sur tous les fonds, comme les coordonnées du viseur de macOS.
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) rt->DrawTextLayout(D2D1::Point2F(pad + dx * scale * 0.8f, pad + dy * scale * 0.8f), layout.Get(), halo.Get());
    rt->DrawTextLayout(D2D1::Point2F(pad, pad), layout.Get(), ink.Get());
    if (FAILED(rt->EndDraw())) return;
    std::vector<std::uint8_t> px(std::size_t(w) * h * 4);
    if (FAILED(bitmap->CopyPixels(nullptr, w * 4, UINT(px.size()), px.data()))) return;
    // À droite et en dessous du curseur ; contre le bord, de l'autre côté.
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const LONG gap = LONG(std::lround(10 * scale));
    POINT at{pt.x + gap, pt.y + gap};
    if (at.x + LONG(w) > mi.rcMonitor.right) at.x = pt.x - gap - LONG(w);
    if (at.y + LONG(h) > mi.rcMonitor.bottom) at.y = pt.y - gap - LONG(h);
    label_.show(px, w, h, at);
}

LRESULT CALLBACK ShotViewfinder::catcherProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE)
        SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<ShotViewfinder*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    return self ? self->handle(h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ShotViewfinder::handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_SETCURSOR:
            SetCursor(windowMode_ && camera_ ? camera_ : cross_);
            return TRUE;
        case WM_MOUSEMOVE: {
            if (!active_) return 0;
            POINT pt;
            GetCursorPos(&pt);
            onMove(pt);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!active_) return 0;
            POINT pt;
            GetCursorPos(&pt);
            if (windowMode_) {   // capture au relâchement : il n'arrive pas, orphelin, à l'app visée
                hover(pt);
                if (!hovered_) return 0;   // le bureau : rien à capturer
                windowPressed_ = true;
                SetCapture(h);
                return 0;
            }
            MONITORINFO mi{sizeof mi};
            GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
            bounds_ = mi.rcMonitor;
            anchor_ = pt;
            dragging_ = true;
            SetCapture(h);
            onMove(pt);
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!active_) return 0;
            POINT pt;
            GetCursorPos(&pt);
            if (windowPressed_) {
                windowPressed_ = false;   // avant ReleaseCapture (WM_CAPTURECHANGED)
                hover(pt);
                if (!hovered_ || !windowMode_) {
                    ReleaseCapture();
                    return 0;
                }
                Result r;
                r.kind = Result::Kind::Window;
                r.window = hovered_;
                r.rect = windowFrameBounds(hovered_);
                r.shadow = (GetAsyncKeyState(VK_MENU) & 0x8000) == 0;   // Alt : sans ombre
                r.monitor = MonitorFromWindow(hovered_, MONITOR_DEFAULTTONEAREST);
                finish(r);   // le viseur n'existe plus : rien après
                return 0;
            }
            if (!dragging_) return 0;
            dragging_ = false;
            ReleaseCapture();
            const RECT r = selectionRect(anchor_, pt, bounds_);
            selection_.hide();
            if (!selectionUsable(r)) {   // un simple clic : le viseur reste ouvert
                onMove(pt);
                return 0;
            }
            Result res;
            res.kind = Result::Kind::Region;
            res.rect = r;
            res.monitor = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
            finish(res);
            return 0;
        }
        case WM_RBUTTONDOWN:   // clic droit : annule au relâchement (sinon il ouvrirait un menu contextuel dessous)
            if (active_) {
                rightPressed_ = true;
                SetCapture(h);
            }
            return 0;
        case WM_RBUTTONUP:
            if (active_ && rightPressed_) cancel();
            return 0;
        case WM_CAPTURECHANGED:
            if (active_ && dragging_) {
                dragging_ = false;
                selection_.hide();
            }
            rightPressed_ = windowPressed_ = false;
            return 0;
        case WM_NCHITTEST: return HTCLIENT;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace md
