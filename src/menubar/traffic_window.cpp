#include "traffic_window.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "../core/log.h"

namespace md {

TrafficWindow* TrafficWindow::self_ = nullptr;

namespace {
constexpr wchar_t kLightsClass[] = L"MacMenuBarLights";
constexpr UINT_PTR kSampleTimer = 1;   // couleur remesurée 200 ms après le dernier déplacement

LightsWindowInfo readInfo(HWND h) {
    LightsWindowInfo w;
    w.style = LONG(GetWindowLongPtrW(h, GWL_STYLE));
    w.exStyle = LONG(GetWindowLongPtrW(h, GWL_EXSTYLE));
    w.classStyle = UINT(GetClassLongPtrW(h, GCL_STYLE));
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    w.className = cls;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &w.frame, sizeof w.frame))) GetWindowRect(h, &w.frame);
    RECT c{};
    GetClientRect(h, &c);
    POINT o{0, 0};
    ClientToScreen(h, &o);
    w.client = RECT{o.x, o.y, o.x + c.right, o.y + c.bottom};
    w.zoomed = IsZoomed(h) != FALSE;
    w.iconic = IsIconic(h) != FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    w.ownProcess = pid == GetCurrentProcessId();
    return w;
}
} // namespace

bool TrafficWindow::create(HINSTANCE instance) {
    if (hwnd_) return true;
    self_ = this;
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kLightsClass;
    RegisterClassExW(&wc);
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kLightsClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr,
                            nullptr, instance, nullptr);
    if (!hwnd_) {
        log::warn(L"Feux tricolores : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    SetWindowDisplayAffinity(hwnd_, WDA_EXCLUDEFROMCAPTURE);   // la mesure de la couleur ne le voit pas
    return true;
}

void TrafficWindow::destroy() {
    detach();
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (self_ == this) self_ = nullptr;
}

void TrafficWindow::unhook() {
    if (hook_) UnhookWinEvent(hook_);
    hook_ = nullptr;
}

void TrafficWindow::detach() {
    unhook();
    target_ = nullptr;
    pressed_ = -1;
    state_.hover = false;
    hide();
}

void TrafficWindow::hide() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    if (hwnd_) KillTimer(hwnd_, kSampleTimer);
}

void TrafficWindow::attach(HWND target, LightsMode mode) {
    if (!hwnd_) return;
    mode_ = mode;
    if (target != target_) {
        detach();
        if (!target || !IsWindow(target)) return;
        target_ = target;
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        // Cible détruite, masquée, montrée, déplacée ou réordonnée : seulement les événements de son processus.
        hook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onEvent, pid, 0,
                                WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        painted_ = false;
    }
    place(true);
}

void CALLBACK TrafficWindow::onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    TrafficWindow* t = self_;
    if (!t || !t->target_) return;
    if (event == EVENT_OBJECT_REORDER) {   // ordre d'affichage changé dans ce processus : on reste au-dessus
        if (t->shown_) t->raise();
        return;
    }
    if (hwnd != t->target_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    switch (event) {
        case EVENT_OBJECT_DESTROY: t->detach(); break;
        case EVENT_OBJECT_HIDE: t->hide(); break;
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_LOCATIONCHANGE:
            t->place(false);
            if (t->shown_) SetTimer(t->hwnd_, kSampleTimer, 200, nullptr);
            break;
        default: break;
    }
}

void TrafficWindow::sample(const RECT& frame, UINT dpi) {
    // Ligne à 4 pt sous le haut du cadre (au-dessus du texte du titre), à droite du calque.
    const LONG y = frame.top + std::lround(4.0 * dpi / 96);
    std::vector<std::uint32_t> samples;
    if (HDC dc = GetDC(nullptr)) {
        for (int i = 0; i < 9; ++i) {
            const LONG x = layout_.window.right + 4 + i * std::lround(10.0 * dpi / 96);
            if (x >= frame.right - 8) break;
            const COLORREF c = GetPixel(dc, x, y);
            if (c != CLR_INVALID) samples.push_back((GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c));
        }
        ReleaseDC(nullptr, dc);
    }
    if (samples.empty()) return;
    const std::uint32_t color = dominantColor(samples);
    if (color == state_.patchColor && painted_) return;
    state_.patchColor = color;
    const double lum = 0.299 * ((color >> 16) & 0xFF) + 0.587 * ((color >> 8) & 0xFF) + 0.114 * (color & 0xFF);
    state_.dark = lum < 128;
    painted_ = false;
}

void TrafficWindow::place(bool resample) {
    if (!hwnd_ || !target_) return;
    if (!IsWindow(target_)) {
        detach();
        return;
    }
    const LightsWindowInfo info = readInfo(target_);
    const UINT dpi = GetDpiForWindow(target_);
    if (!IsWindowVisible(target_) || !wantsLights(info, mode_, dpi)) {
        hide();
        return;
    }
    const LightsLayout l = lightsLayout(info.frame, info.client, dpi);
    const bool resized = l.window.right - l.window.left != layout_.window.right - layout_.window.left ||
                         l.window.bottom - l.window.top != layout_.window.bottom - layout_.window.top;
    layout_ = l;
    scale_ = dpi / 96.0;
    const bool enabled[3] = {!(info.classStyle & CS_NOCLOSE), (info.style & WS_MINIMIZEBOX) != 0, (info.style & WS_MAXIMIZEBOX) != 0};
    for (int i = 0; i < 3; ++i)
        if (state_.enabled[i] != enabled[i]) {
            state_.enabled[i] = enabled[i];
            painted_ = false;
        }
    if (resample || !painted_) sample(info.frame, dpi);
    if (resized) painted_ = false;
    if (!painted_) paint();
    else {
        POINT pos{layout_.window.left, layout_.window.top};
        UpdateLayeredWindow(hwnd_, nullptr, &pos, nullptr, nullptr, nullptr, 0, nullptr, 0);   // même image, déplacée
    }
    raise();
}

void TrafficWindow::paint() {
    const int w = int(layout_.window.right - layout_.window.left), h = int(layout_.window.bottom - layout_.window.top);
    if (w <= 0 || h <= 0) return;
    const auto px = renderLights(layout_, state_, scale_);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        std::memcpy(bits, px.data(), px.size());
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{layout_.window.left, layout_.window.top}, zero{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd_, screen, &pos, &size, mem, &zero, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        painted_ = true;
        paintedSize_ = size;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void TrafficWindow::raise() {
    if (!hwnd_ || !target_) return;
    const HWND above = GetWindow(target_, GW_HWNDPREV);
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    HWND after = HWND_TOP;   // la cible est au premier plan : le haut des fenêtres ordinaires
    if (above == hwnd_) flags |= SWP_NOZORDER;   // déjà juste au-dessus
    else if (above && !(GetWindowLongPtrW(above, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = above;
    SetWindowPos(hwnd_, after, 0, 0, 0, 0, flags);
    shown_ = true;
}

LRESULT CALLBACK TrafficWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_ && self_->hwnd_ == hwnd) return self_->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrafficWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    const auto screenPoint = [&] {
        POINT p{short(LOWORD(lp)), short(HIWORD(lp))};
        ClientToScreen(hwnd_, &p);
        return p;
    };
    const auto overGroup = [&](POINT p) {
        return p.x >= layout_.circles[0].left - 2 && p.x <= layout_.circles[2].right + 2 && p.y >= layout_.circles[0].top - 2 &&
               p.y <= layout_.circles[0].bottom + 2;
    };
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // la fenêtre de l'app reste active
        case WM_MOUSEMOVE: {
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
            }
            const bool hover = overGroup(screenPoint());
            if (hover != state_.hover) {
                state_.hover = hover;   // symboles ×, −, + sur les trois, comme sur macOS
                paint();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (state_.hover) {
                state_.hover = false;
                paint();
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT p = screenPoint();
            const int hit = hitLight(layout_, p);
            if (hit >= 0) {
                if (state_.enabled[hit]) {
                    pressed_ = hit;
                    SetCapture(hwnd_);
                }
                return 0;
            }
            // Le fond appartient à la barre de titre : déplacer la fenêtre, double-clic = zoom (comportement de Windows).
            ReleaseCapture();
            if (target_)
                PostMessageW(target_, msg == WM_LBUTTONDBLCLK ? WM_NCLBUTTONDBLCLK : WM_NCLBUTTONDOWN, HTCAPTION,
                             MAKELPARAM(p.x, p.y));
            return 0;
        }
        case WM_LBUTTONUP: {
            if (pressed_ < 0) return 0;
            const int pressed = pressed_;
            pressed_ = -1;
            ReleaseCapture();
            if (hitLight(layout_, screenPoint()) == pressed && target_ && IsWindow(target_))
                PostMessageW(target_, WM_SYSCOMMAND, lightCommand(pressed, IsZoomed(target_) != FALSE), 0);
            return 0;
        }
        case WM_TIMER:
            if (wp == kSampleTimer) {
                KillTimer(hwnd_, kSampleTimer);
                place(true);
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace md
