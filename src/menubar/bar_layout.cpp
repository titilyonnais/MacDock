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
    double rx = in.barWidth - in.rightMargin - rightPartWidth(in);
    const double rightStart = rx;
    for (std::size_t i = 0; i < in.rightWidths.size(); ++i) {
        if (i + 1 == in.rightWidths.size()) rx += in.lastGap;
        l.rightX.push_back(rx);
        rx += in.rightWidths[i];
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

double rightPartWidth(const BarLayoutInput& in) {
    if (in.rightWidths.empty()) return 0;
    return std::accumulate(in.rightWidths.begin(), in.rightWidths.end(), 0.0) + in.lastGap;
}

double textBoxWidth(double textPx, double scale, double padding) { return textPx / scale + 2 * padding; }

} // namespace md
