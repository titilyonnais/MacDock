#include "transition_gate.h"

#include <dwmapi.h>

namespace md {

TransitionApi realTransitionApi() {
    TransitionApi api;
    api.disable = [](HWND h, bool off) {
        const BOOL v = off ? TRUE : FALSE;
        return SUCCEEDED(DwmSetWindowAttribute(h, DWMWA_TRANSITIONS_FORCEDISABLED, &v, sizeof v));
    };
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

void TransitionGate::releaseAll() {
    for (const auto& [h, until] : held_)
        if (!api_.alive || api_.alive(h)) api_.disable(h, false);
    held_.clear();
}

} // namespace md
