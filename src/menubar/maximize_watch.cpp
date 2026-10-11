#include "maximize_watch.h"

namespace md {

MaximizeWatch* MaximizeWatch::self_ = nullptr;

BOOL CALLBACK MaximizeWatch::seedOne(HWND h, LPARAM self) {
    auto* w = reinterpret_cast<MaximizeWatch*>(self);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == w->pid_) w->zoom_.seed(h, IsZoomed(h) != FALSE);
    return TRUE;
}

void MaximizeWatch::follow(DWORD pid) {
    if (pid == pid_ && (hook_ || !pid)) return;
    stop();
    if (!pid || pid == GetCurrentProcessId()) return;
    pid_ = pid;
    EnumWindows(seedOne, reinterpret_cast<LPARAM>(this));
    self_ = this;
    hook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onEvent, pid, 0,
                            WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
}

void MaximizeWatch::stop() {
    if (hook_) UnhookWinEvent(hook_);
    hook_ = nullptr;
    pid_ = 0;
    zoom_.clear();
    if (self_ == this) self_ = nullptr;
}

void CALLBACK MaximizeWatch::onEvent(HWINEVENTHOOK hook, DWORD, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    MaximizeWatch* w = self_;
    if (!w || hook != w->hook_ || !hwnd || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    if (GetAncestor(hwnd, GA_ROOT) != hwnd) return;   // contrôles et fenêtres enfants : pas d'état agrandi
    if (w->zoom_.changed(hwnd, IsZoomed(hwnd) != FALSE) && w->sink_)
        PostMessageW(w->sink_, w->message_, reinterpret_cast<WPARAM>(hwnd), 0);
}

} // namespace md
