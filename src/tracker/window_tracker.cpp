#include "window_tracker.h"

#include <algorithm>
#include <vector>

#include "../core/log.h"
#include "app_identity.h"
#include "win_event_filter.h"

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
    winEventMsg_ = RegisterWindowMessageW(L"MacDockWinEvent");
    RegisterShellHookWindow(msgWindow_);
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    eventThread_ = std::thread([this, ready] {
        MSG m;
        PeekMessageW(&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // file de messages du fil, avant tout message reçu
        eventThreadId_ = GetCurrentThreadId();
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        hooks_[0] = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, winEventProc, 0, 0, flags);
        hooks_[1] = SetWinEventHook(EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND, nullptr, winEventProc, 0, 0, flags);
        // Plages précises : surtout pas EVENT_OBJECT_LOCATIONCHANGE (émis à chaque mouvement de souris).
        hooks_[2] = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_HIDE, nullptr, winEventProc, 0, 0, flags);
        hooks_[3] = SetWinEventHook(EVENT_OBJECT_NAMECHANGE, EVENT_OBJECT_NAMECHANGE, nullptr, winEventProc, 0, 0, flags);
        hooks_[4] = SetWinEventHook(EVENT_OBJECT_CLOAKED, EVENT_OBJECT_UNCLOAKED, nullptr, winEventProc, 0, 0, flags);
        hooksOk_ = hooks_[0] && hooks_[1] && hooks_[2] && hooks_[3] && hooks_[4];
        SetEvent(ready);
        while (GetMessageW(&m, nullptr, 0, 0) > 0)
            if (m.message == kWatchMoves) setMoveHook(DWORD(m.wParam));
        for (auto& h : hooks_)
            if (h) { UnhookWinEvent(h); h = nullptr; }
    });
    if (ready) {
        WaitForSingleObject(ready, 5000);
        CloseHandle(ready);
    }
    rescan();
    watchMoves(GetForegroundWindow());
    return hooksOk_;
}

void WindowTracker::stop() {
    if (eventThread_.joinable()) {   // le fil retire ses crochets en sortant
        if (const DWORD id = eventThreadId_) PostThreadMessageW(id, WM_QUIT, 0, 0);
        eventThread_.join();
    }
    eventThreadId_ = 0;
    movePid_ = 0;
    if (msgWindow_) {
        DeregisterShellHookWindow(msgWindow_);
        KillTimer(msgWindow_, kPendingTimer);
    }
    if (instance_ == this) instance_ = nullptr;
    msgWindow_ = nullptr;
}

void CALLBACK WindowTracker::winEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild,
                                          DWORD, DWORD) {
    // Fil des crochets : seuls les événements d'une fenêtre réveillent le fil du Dock.
    WindowTracker* t = instance_;
    if (!t) return;
    if (t->trace_) {   // mode diagnostic : tout ce qui arrive, filtré ou non
        DWORD pid = 0;
        if (hwnd) GetWindowThreadProcessId(hwnd, &pid);
        std::lock_guard lock(t->rawMutex_);
        ++t->rawCounts_[event];
        ++t->rawByProcess_[pid];
    }
    if (forwardedWinEvent(hwnd, idObject, idChild))
        PostMessageW(t->msgWindow_, t->winEventMsg_, WPARAM(event), reinterpret_cast<LPARAM>(hwnd));
}

void WindowTracker::onEvent(DWORD event, HWND hwnd) {
    if (trace_) {
        ++eventCounts_[event];
        ULONGLONG now = GetTickCount64();
        if (now - countSince_ > 5000) {
            std::wstring s;
            for (auto& [e, n] : eventCounts_) s += std::to_wstring(e) + L"=" + std::to_wstring(n) + L" ";
            std::wstring raw, procs;
            std::vector<std::pair<int, DWORD>> top;
            std::lock_guard lock(rawMutex_);
            for (auto& [e, n] : rawCounts_) raw += std::to_wstring(e) + L"=" + std::to_wstring(n) + L" ";
            for (auto& [pid, n] : rawByProcess_) top.push_back({n, pid});
            std::sort(top.rbegin(), top.rend());
            for (std::size_t i = 0; i < top.size() && i < 3; ++i)
                procs += L"pid " + std::to_wstring(top[i].second) + L"=" + std::to_wstring(top[i].first) + L" ";
            log::info(L"[perf] événements fenêtres en 5 s : %s; reçus avant filtre : %s; processus : %s", s.c_str(), raw.c_str(),
                      procs.c_str());
            rawCounts_.clear();
            rawByProcess_.clear();
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
        case EVENT_OBJECT_LOCATIONCHANGE:
            if (known_.contains(hwnd) && events_.moved) events_.moved(hwnd);
            break;
        case EVENT_SYSTEM_FOREGROUND:
            foreground_ = hwnd;
            watchMoves(hwnd);
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

// EVENT_OBJECT_LOCATIONCHANGE pour tout le système arriverait à chaque mouvement de souris : on ne l'écoute que
// pour le processus au premier plan (c'est lui qu'on réduit), et on change de processus avec le premier plan.
void WindowTracker::watchMoves(HWND foreground) {
    DWORD pid = 0;
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (pid == movePid_) return;
    movePid_ = pid;
    if (const DWORD id = eventThreadId_) PostThreadMessageW(id, kWatchMoves, WPARAM(pid), 0);
}

void WindowTracker::setMoveHook(DWORD pid) {
    if (hooks_[5]) UnhookWinEvent(hooks_[5]);
    hooks_[5] = nullptr;
    if (pid && pid != GetCurrentProcessId())
        hooks_[5] = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, winEventProc, pid, 0,
                                    WINEVENT_OUTOFCONTEXT);
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
    if (msg == winEventMsg_ && winEventMsg_) {   // événement de fenêtre, posté par le fil des crochets
        onEvent(DWORD(wp), reinterpret_cast<HWND>(lp));
        return true;
    }
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
