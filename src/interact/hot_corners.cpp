#include "hot_corners.h"

#include <cstdlib>

namespace md {

namespace {
constexpr LONG kZone = 2, kRearm = 24;

bool onAnyScreen(LONG x, LONG y, const std::vector<RECT>& monitors) {
    for (const RECT& r : monitors)
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return true;
    return false;
}
} // namespace

std::optional<Corner> cornerAt(POINT pt, const std::vector<RECT>& monitors) {
    for (const RECT& r : monitors) {
        if (pt.x < r.left || pt.x >= r.right || pt.y < r.top || pt.y >= r.bottom) continue;
        const bool left = pt.x < r.left + kZone, right = pt.x >= r.right - kZone;
        const bool top = pt.y < r.top + kZone, bottom = pt.y >= r.bottom - kZone;
        if (!(left || right) || !(top || bottom)) return std::nullopt;
        const LONG cx = left ? r.left : r.right - 1, cy = top ? r.top : r.bottom - 1;   // pixel du coin
        const LONG dx = left ? -1 : 1, dy = top ? -1 : 1;
        if (onAnyScreen(cx + dx, cy, monitors) || onAnyScreen(cx, cy + dy, monitors) || onAnyScreen(cx + dx, cy + dy, monitors))
            return std::nullopt;
        return top ? (left ? Corner::TopLeft : Corner::TopRight) : (left ? Corner::BottomLeft : Corner::BottomRight);
    }
    return std::nullopt;
}

std::optional<Corner> HotCornerTracker::update(std::optional<Corner> at, POINT pt, bool blocked) {
    if (!at) {
        if (!armed_ && (std::labs(pt.x - last_.x) > kRearm || std::labs(pt.y - last_.y) > kRearm)) armed_ = true;
        return std::nullopt;
    }
    last_ = pt;
    if (!armed_) return std::nullopt;
    armed_ = false;
    if (blocked) return std::nullopt;
    return at;
}

} // namespace md
