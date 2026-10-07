#include "bar_layout.h"

#include <numeric>

namespace md {

BarLayout layoutBar(const BarLayoutInput& in) {
    BarLayout l;
    double x = in.leftMargin;
    for (double w : in.leftWidths) {
        l.leftX.push_back(x);
        x += w;
    }
    const double rightTotal = std::accumulate(in.rightWidths.begin(), in.rightWidths.end(), 0.0);
    double rx = in.barWidth - in.rightMargin - rightTotal;
    const double rightStart = rx;
    for (double w : in.rightWidths) {
        l.rightX.push_back(rx);
        rx += w;
    }
    const double limit = rightStart - in.minGap;
    for (std::size_t i = 0; i < in.leftWidths.size(); ++i) {
        if (i >= in.keepLeft && l.leftX[i] + in.leftWidths[i] > limit) break;
        l.leftVisible = i + 1;
    }
    return l;
}

BarHit hitTestBar(const BarLayout& l, const BarLayoutInput& in, double x) {
    BarHit h;
    for (std::size_t i = 0; i < l.leftVisible && i < in.leftWidths.size(); ++i)
        if (x >= l.leftX[i] && x < l.leftX[i] + in.leftWidths[i]) {
            h.kind = BarHit::Kind::Left;
            h.index = i;
            return h;
        }
    for (std::size_t i = 0; i < l.rightX.size() && i < in.rightWidths.size(); ++i)
        if (x >= l.rightX[i] && x < l.rightX[i] + in.rightWidths[i]) {
            h.kind = BarHit::Kind::Right;
            h.index = i;
            return h;
        }
    return h;
}

} // namespace md
