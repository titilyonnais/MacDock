#include "bar_screens.h"

#include <algorithm>

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

} // namespace md
