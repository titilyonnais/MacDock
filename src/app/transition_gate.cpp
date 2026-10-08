#include "transition_gate.h"

#include <dwmapi.h>

namespace md {

namespace {
constexpr wchar_t kHeldProp[] = L"MacDockTransitionsHeld";

bool setDisabled(HWND h, bool off) {
    const BOOL v = off ? TRUE : FALSE;
    const bool ok = SUCCEEDED(DwmSetWindowAttribute(h, DWMWA_TRANSITIONS_FORCEDISABLED, &v, sizeof v));
    if (off) SetPropW(h, kHeldProp, reinterpret_cast<HANDLE>(1));
    else RemovePropW(h, kHeldProp);
    return ok;
}

struct Orphans {
    DWORD process;
    int count = 0;
};
BOOL CALLBACK releaseIfMarked(HWND h, LPARAM lp) {
    auto* o = reinterpret_cast<Orphans*>(lp);
    if (!GetPropW(h, kHeldProp)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (o->process && pid != o->process) return TRUE;
    setDisabled(h, false);
    ++o->count;
    return TRUE;
}
} // namespace

bool transitionsMarked(HWND window) { return GetPropW(window, kHeldProp) != nullptr; }

int releaseOrphanTransitions(DWORD onlyProcess) {
    Orphans o{onlyProcess};
    EnumWindows(releaseIfMarked, reinterpret_cast<LPARAM>(&o));
    return o.count;
}

TransitionApi realTransitionApi() {
    TransitionApi api;
    api.disable = setDisabled;
    api.alive = [](HWND h) { return IsWindow(h) != FALSE; };
    return api;
}

void TransitionGate::hold(HWND window, double until) {
    if (!window) return;
    auto it = held_.find(window);
    if (it != held_.end()) {
        if (until > it->second) it->second = until;
        return;
    }
    if (api_.disable) api_.disable(window, true);
    held_[window] = until;
}

void TransitionGate::release(double now, const std::function<bool(HWND)>& busy) {
    for (auto it = held_.begin(); it != held_.end();) {
        const HWND h = it->first;
        if (api_.alive && !api_.alive(h)) {   // fermée : rien à rendre
            it = held_.erase(it);
        } else if (now >= it->second && !(busy && busy(h))) {
            if (api_.disable) api_.disable(h, false);
            it = held_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<HWND> TransitionGate::windows() const {
    std::vector<HWND> out;
    for (const auto& [h, until] : held_) out.push_back(h);
    return out;
}

void TransitionGate::releaseAll() {
    for (const auto& [h, until] : held_)
        if (!api_.alive || api_.alive(h)) api_.disable(h, false);
    held_.clear();
}

} // namespace md
