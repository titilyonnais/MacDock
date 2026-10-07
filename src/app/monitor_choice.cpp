#include "monitor_choice.h"

#include "../core/strings.h"

namespace md {

namespace {

bool contains(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

bool insideAny(const std::vector<MonitorInfo>& monitors, POINT p) {
    for (const auto& m : monitors)
        if (contains(m.rect, p)) return true;
    return false;
}

} // namespace

std::optional<std::size_t> pushedMonitor(const std::vector<MonitorInfo>& monitors, POINT cursor, DockPosition edge,
                                         int edgePx) {
    for (std::size_t i = 0; i < monitors.size(); ++i) {
        const RECT& r = monitors[i].rect;
        if (!contains(r, cursor)) continue;
        bool atEdge = false;
        POINT beyond{};   // point juste derrière le bord : sur un autre écran, ce n'est pas un vrai bord
        switch (edge) {
            case DockPosition::Left:
                atEdge = cursor.x <= r.left + edgePx;
                beyond = {r.left - 1, cursor.y};
                break;
            case DockPosition::Right:
                atEdge = cursor.x >= r.right - 1 - edgePx;
                beyond = {r.right, cursor.y};
                break;
            default:
                atEdge = cursor.y >= r.bottom - 1 - edgePx;
                beyond = {cursor.x, r.bottom};
        }
        if (atEdge && !insideAny(monitors, beyond)) return i;
        return std::nullopt;
    }
    return std::nullopt;
}

std::size_t initialMonitor(const std::vector<MonitorInfo>& monitors, const std::wstring& saved) {
    if (!saved.empty()) {
        const std::wstring want = toLower(saved);
        for (std::size_t i = 0; i < monitors.size(); ++i)
            if (toLower(monitors[i].name) == want) return i;
    }
    for (std::size_t i = 0; i < monitors.size(); ++i)
        if (monitors[i].primary) return i;
    return 0;
}

std::wstring ScreenPush::update(const std::wstring& target, double now) {
    if (target.empty() || target != target_ || now - last_ > kMaxSilence) {
        target_ = target;
        since_ = now;
        last_ = now;
        return {};
    }
    last_ = now;
    if (now - since_ < kPushSeconds) return {};
    std::wstring chosen = target_;
    target_.clear();   // une seule fois par poussée
    return chosen;
}

} // namespace md
