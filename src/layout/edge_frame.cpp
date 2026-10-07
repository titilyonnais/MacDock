#include "edge_frame.h"

#include <cmath>

namespace md {

POINT EdgeFrame::toLocal(POINT w) const {
    switch (edge) {
        case DockPosition::Left: return POINT{w.y, LONG(std::lround(cross())) - w.x};
        case DockPosition::Right: return POINT{w.y, w.x};
        default: return w;
    }
}

EdgePoint EdgeFrame::toWindow(double x, double y) const {
    switch (edge) {
        case DockPosition::Left: return EdgePoint{cross() - y, x};
        case DockPosition::Right: return EdgePoint{y, x};
        default: return EdgePoint{x, y};
    }
}

} // namespace md
