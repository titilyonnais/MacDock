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
    // Sortie du plein écran : là tout de suite, comme sur Mac (la bande réservée ne reste pas vide le temps d'une
    // glissade pendant que la fenêtre reprend sa taille).
    const bool backFromFullscreen = wasFullscreen_ && !in.fullscreen && raw;
    wasFullscreen_ = in.fullscreen;
    if (backFromFullscreen) {
        target_ = true;
        p_ = 1;
    }
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

HWND fullscreenWindowOn(HMONITOR monitor, const RECT& monitorRect) {
    if (!monitor) return nullptr;
    std::vector<ZWindow> order;
    std::vector<HWND> handles;
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
        handles.push_back(h);
        if (z.onMonitor && !z.topmost) break;   // la décision est prise à cette fenêtre
    }
    if (!fullscreenOnMonitor(order, monitorRect)) return nullptr;
    for (std::size_t i = 0; i < order.size(); ++i)   // la fenêtre qui couvre l'écran
        if (order[i].onMonitor && isFullscreenWindow(order[i].rect, monitorRect, order[i].zoomed, order[i].caption))
            return handles[i];
    return nullptr;
}

namespace {
std::vector<FullscreenWatch*>& watches() {   // fil de l'interface seulement
    static std::vector<FullscreenWatch*> w;
    return w;
}
} // namespace

void FullscreenWatch::watch(HWND window, std::function<void()> changed) {
    changed_ = std::move(changed);
    if (window == window_) return;
    stop();
    if (!window) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (!pid) return;
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    hooks_[0] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, onEvent, pid, 0, flags);
    hooks_[1] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onEvent, pid, 0, flags);
    window_ = window;
    watches().push_back(this);
}

void FullscreenWatch::stop() {
    for (HWINEVENTHOOK& h : hooks_) {
        if (h) UnhookWinEvent(h);
        h = nullptr;
    }
    window_ = nullptr;
    auto& w = watches();
    w.erase(std::remove(w.begin(), w.end(), this), w.end());
}

void CALLBACK FullscreenWatch::onEvent(HWINEVENTHOOK hook, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    for (FullscreenWatch* w : std::vector<FullscreenWatch*>(watches()))   // copie : le rappel peut tout changer
        if ((w->hooks_[0] == hook || w->hooks_[1] == hook) && hwnd == w->window_ && w->changed_) {
            const auto changed = w->changed_;
            changed();
            return;
        }
}

} // namespace md
