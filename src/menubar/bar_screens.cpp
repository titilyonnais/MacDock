#include "bar_screens.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace md {

std::vector<ScreenInfo> orderScreens(std::vector<ScreenInfo> s) {
    std::stable_sort(s.begin(), s.end(), [](const ScreenInfo& a, const ScreenInfo& b) {
        if (a.primary != b.primary) return a.primary;
        if (a.rect.left != b.rect.left) return a.rect.left < b.rect.left;
        return a.rect.top < b.rect.top;
    });
    return s;
}

std::size_t activeScreen(const std::vector<ScreenInfo>& s, const RECT* foreground, POINT cursor) {
    if (foreground) {
        long long best = 0;
        std::size_t index = s.size();
        for (std::size_t i = 0; i < s.size(); ++i) {
            RECT in{};
            if (!IntersectRect(&in, foreground, &s[i].rect)) continue;
            const long long area = (long long)(in.right - in.left) * (in.bottom - in.top);
            if (area > best) {
                best = area;
                index = i;
            }
        }
        if (index < s.size()) return index;
    }
    for (std::size_t i = 0; i < s.size(); ++i)
        if (PtInRect(&s[i].rect, cursor)) return i;
    return 0;
}

double barWidthPoints(const ScreenInfo& s) {
    return double(s.rect.right - s.rect.left) * 96.0 / double(s.dpi ? s.dpi : 96);
}

ScreenPlan planScreens(const std::vector<RECT>& existing, const std::vector<ScreenInfo>& wanted) {
    ScreenPlan p;
    std::vector<bool> used(existing.size(), false);
    for (const auto& w : wanted) {
        int keep = -1;
        for (std::size_t i = 0; i < existing.size() && keep < 0; ++i)
            if (!used[i] && EqualRect(&existing[i], &w.rect)) {
                used[i] = true;
                keep = int(i);
            }
        p.keep.push_back(keep);
    }
    for (std::size_t i = 0; i < existing.size(); ++i)
        if (!used[i]) p.drop.push_back(i);
    return p;
}

bool RebuildGate::tryBegin(bool blocked) {
    if (blocked || running) {
        pending = true;
        return false;
    }
    running = true;
    pending = false;
    return true;
}

bool RebuildGate::end() {
    running = false;
    return pending;
}

bool cursorAtTopEdge(const RECT& screen, POINT pt) { return PtInRect(&screen, pt) && pt.y <= screen.top + 1; }

bool cursorInBar(const RECT& screen, int height, POINT pt) {
    return pt.x >= screen.left && pt.x < screen.right && pt.y >= screen.top && pt.y < screen.top + height;
}

std::size_t trayFit(const BarLayoutInput& in, std::size_t trayCount, double trayWidth) {
    if (trayWidth <= 0) return trayCount;
    double kept = 0;
    for (std::size_t i = 0; i < in.leftWidths.size() && i < in.keepLeft; ++i) kept += in.leftWidths[i];
    const double right = std::accumulate(in.rightWidths.begin(), in.rightWidths.end(), 0.0);
    const double room = in.barWidth - in.leftMargin - in.rightMargin - kept - in.minGap - right;
    if (room <= 0) return 0;
    return std::min(trayCount, std::size_t(std::floor(room / trayWidth)));
}

} // namespace md
