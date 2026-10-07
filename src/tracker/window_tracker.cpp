#include "window_tracker.h"

#include "../core/log.h"
#include "app_identity.h"

namespace md {

WindowTracker* WindowTracker::instance_ = nullptr;

namespace {
constexpr UINT_PTR kPendingTimer = 0x4D44;   // "MD"
constexpr ULONGLONG kPendingTimeoutMs = 15000;

std::wstring windowTitle(HWND hwnd) {
    wchar_t buf[512] = {};
    GetWindowTextW(hwnd, buf, 512);
    return buf;
}
} // namespace

bool WindowTracker::start(HWND messageWindow, Events events) {
    msgWindow_ = messageWindow;
    events_ = std::move(events);
    instance_ = this;
    shellMsg_ = RegisterWindowMessageW(L"SHELLHOOK");
    RegisterShellHookWindow(msgWindow_);
    const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
    hooks_[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEventProc, 0, 0, flags);
    hooks_[1] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, winEventProc, 0, 0, flags);
    // Plages précises : surtout pas EVENT_OBJECT_LOCATIONCHANGE (émis à chaque mouvement de souris).
    hooks_[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, winEventProc, 0, 0, flags);
    hooks_[3] = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, winEventProc, 0, 0, flags);
    hooks_[4] = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, winEventProc, 0, 0, flags);
    rescan();
    for (auto h : hooks_)
        if (!h) return false;
    return true;
}

void WindowTracker::stop() {
    for (auto& h : hooks_)
        if (h) { UnhookWinEvent(h); h = nullptr; }
    if (msgWindow_) {
        DeregisterShellHookWindow(msgWindow_);
        KillTimer(msgWindow_, kPendingTimer);
    }
    if (instance_ == this) instance_ = nullptr;
    msgWindow_ = nullptr;
}

void CALLBACK WindowTracker::winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                          DWORD, DWORD) {
    if (!instance_ || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    instance_->onEvent(event, hwnd);
}

void WindowTracker::onEvent(DWORD event, HWND hwnd) {
    if (trace_) {
        ++eventCounts_[event];
        ULONGLONG now = GetTickCount64();
        if (now - countSince_ > 5000) {
            std::wstring s;
            for (auto& [e, n] : eventCounts_) s += std::to_wstring(e) + L"=" + std::to_wstring(n) + L" ";
            log::info(L"[perf] événements fenêtres en 5 s : %s", s.c_str());
            eventCounts_.clear();
            countSince_ = now;
        }
    }
    switch (event) {
        case EVENT_OBJECT_DESTROY:
            forget(hwnd);
            break;
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_HIDE:
        case EVENT_OBJECT_CLOAKED:
        case EVENT_OBJECT_UNCLOAKED:
            if (GetAncestor(hwnd, GA_ROOT) == hwnd || known_.contains(hwnd)) evaluate(hwnd);
            break;
        case EVENT_OBJECT_NAMECHANGE:
            if (auto it = known_.find(hwnd); it != known_.end()) {
                std::wstring t = windowTitle(hwnd);
                if (t != it->second.title) {
                    it->second.title = t;
                    if (events_.titleChanged) events_.titleChanged(hwnd, t);
                }
            } else if (GetAncestor(hwnd, GA_ROOT) == hwnd) {
                evaluate(hwnd);   // un titre qui apparaît peut rendre la fenêtre éligible
            }
            break;
        case EVENT_SYSTEM_FOREGROUND:
            foreground_ = hwnd;
            if (events_.foreground) events_.foreground(hwnd);
            if (!known_.contains(hwnd)) evaluate(hwnd);
            if (known_.contains(hwnd) && events_.activated) events_.activated(hwnd);
            break;
        case EVENT_SYSTEM_MINIMIZESTART:
        case EVENT_SYSTEM_MINIMIZEEND:
            if (auto it = known_.find(hwnd); it != known_.end()) {
                bool minimized = event == EVENT_SYSTEM_MINIMIZESTART;
                if (it->second.minimized != minimized) {
                    it->second.minimized = minimized;
                    if (events_.minimized) events_.minimized(hwnd, minimized);
                    if (minimized && events_.minimizeStarted) events_.minimizeStarted(hwnd);
                }
            }
            break;
        default:
            break;
    }
}

void WindowTracker::evaluate(HWND hwnd) {
    bool eligible = isDockEligibleWindow(hwnd);
    bool isKnown = known_.contains(hwnd);
    if (eligible && !isKnown) {
        auto id = identifyWindow(hwnd);
        if (!id) return;
        pending_.erase(hwnd);
        Known k{windowTitle(hwnd), IsIconic(hwnd) != FALSE};
        known_[hwnd] = k;
        if (events_.opened) events_.opened(hwnd, *id);
        if (events_.titleChanged && !k.title.empty()) events_.titleChanged(hwnd, k.title);
        if (k.minimized && events_.minimized) events_.minimized(hwnd, true);
    } else if (!eligible && isKnown) {
        forget(hwnd);
    } else if (!eligible && IsWindowVisible(hwnd)) {
        wchar_t cls[64] = {};
        GetClassNameW(hwnd, cls, 64);
        if (wcscmp(cls, L"ApplicationFrameWindow") == 0 && !pending_.contains(hwnd)) {
            pending_[hwnd] = GetTickCount64();
            SetTimer(msgWindow_, kPendingTimer, 400, nullptr);
        }
    }
}

void WindowTracker::forget(HWND hwnd) {
    pending_.erase(hwnd);
    auto it = known_.find(hwnd);
    if (it == known_.end()) return;
    known_.erase(it);
    if (events_.closed) events_.closed(hwnd);
}

void WindowTracker::rescan() {
    // Fenêtres disparues pendant qu'on ne regardait pas (redémarrage d'explorer…).
    std::vector<HWND> gone;
    for (auto& [h, k] : known_)
        if (!isDockEligibleWindow(h)) gone.push_back(h);
    for (HWND h : gone) forget(h);
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            reinterpret_cast<WindowTracker*>(lp)->evaluate(h);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    foreground_ = GetForegroundWindow();
}

bool WindowTracker::handleMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == shellMsg_ && shellMsg_) {
        HWND hwnd = reinterpret_cast<HWND>(lp);
        if ((wp & 0x7FFF) == HSHELL_REDRAW && (wp & HSHELL_HIGHBIT)) {   // HSHELL_FLASH
            if (!known_.contains(hwnd)) evaluate(hwnd);
            if (known_.contains(hwnd) && events_.flashed) events_.flashed(hwnd);
        }
        return true;
    }
    if (msg == WM_TIMER && wp == kPendingTimer) {
        ULONGLONG now = GetTickCount64();
        std::vector<HWND> retry;
        for (auto& [h, since] : pending_) retry.push_back(h);
        for (HWND h : retry) {
            if (!IsWindow(h) || now - pending_[h] > kPendingTimeoutMs) { pending_.erase(h); continue; }
            evaluate(h);
        }
        if (pending_.empty()) KillTimer(msgWindow_, kPendingTimer);
        return true;
    }
    return false;
}

} // namespace md
