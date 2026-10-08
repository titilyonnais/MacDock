#include "visibility.h"

#include <dwmapi.h>

#include <algorithm>
#include <cwchar>

namespace md {

bool Visibility::update(const VisibilityInputs& in, double now) {
    const bool raw = !in.fullscreen && (!in.autohide || in.cursorAtEdge || in.cursorInDock || in.menuOpen || in.dragging);
    if (raw != raw_) {
        raw_ = raw;
        rawSince_ = now;
    }
    immediate_ = in.fullscreen;
    if (target_ != raw_) {
        double delay = immediate_ ? 0 : (raw_ ? t_.showDelay : t_.leaveDelay);
        if (now - rawSince_ >= delay) target_ = raw_;
    }

    const double dt = last_ < 0 ? 0 : std::clamp(now - last_, 0.0, 0.1);
    last_ = now;
    const double before = p_;
    if (target_) p_ = std::min(1.0, p_ + dt / std::max(0.01, t_.showSeconds));
    else p_ = std::max(0.0, p_ - dt / std::max(0.01, t_.hideSeconds));
    return p_ != before || (target_ ? p_ < 1 : p_ > 0);
}

double Visibility::shown() const {
    // Ease-in-out (cubique), comme l'animation du Dock de macOS.
    const double x = p_;
    return x < 0.5 ? 4 * x * x * x : 1 - (-2 * x + 2) * (-2 * x + 2) * (-2 * x + 2) / 2;
}

double Visibility::wakeAt() const {
    if (target_ == raw_) return -1;
    return rawSince_ + (immediate_ ? 0 : (raw_ ? t_.showDelay : t_.leaveDelay));
}

bool coversMonitor(const RECT& w, const RECT& m) {
    return w.left <= m.left + 1 && w.top <= m.top + 1 && w.right >= m.right - 1 && w.bottom >= m.bottom - 1;
}

bool isFullscreenWindow(const RECT& window, const RECT& monitor, bool zoomed, bool hasCaption) {
    return coversMonitor(window, monitor) && !(zoomed && hasCaption);
}

bool fullscreenOnMonitor(const std::vector<ZWindow>& zOrder, const RECT& monitor) {
    for (const ZWindow& w : zOrder) {
        if (!w.eligible || !w.onMonitor) continue;
        if (isFullscreenWindow(w.rect, monitor, w.zoomed, w.caption)) return true;
        if (w.topmost) continue;   // vignette ou pense-bête toujours au-dessus : la fenêtre en dessous décide
        return false;
    }
    return false;
}

bool fullscreenWindowOn(HMONITOR monitor, const RECT& monitorRect) {
    if (!monitor) return false;
    std::vector<ZWindow> order;
    const DWORD self = GetCurrentProcessId();
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT)) {
        if (!IsWindowVisible(h)) continue;
        ZWindow z;
        const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE), ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
        DWORD pid = 0, cloaked = 0;
        GetWindowThreadProcessId(h, &pid);
        wchar_t cls[64] = {};
        GetClassNameW(h, cls, 64);
        const bool shell = !wcscmp(cls, L"Progman") || !wcscmp(cls, L"WorkerW") || !wcscmp(cls, L"Shell_TrayWnd") ||
                           !wcscmp(cls, L"Shell_SecondaryTrayWnd");
        z.eligible = pid != self && !shell && !IsIconic(h) && !(ex & (WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)) &&
                     !(SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked);
        if (!z.eligible) continue;
        z.onMonitor = MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == monitor;
        z.topmost = (ex & WS_EX_TOPMOST) != 0;
        z.zoomed = IsZoomed(h) != FALSE;
        z.caption = (style & WS_CAPTION) == WS_CAPTION;
        GetWindowRect(h, &z.rect);
        order.push_back(z);
        if (z.onMonitor && !z.topmost) break;   // la décision est prise à cette fenêtre
    }
    return fullscreenOnMonitor(order, monitorRect);
}

} // namespace md
