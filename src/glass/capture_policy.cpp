#include "capture_policy.h"

#include <algorithm>

namespace md {

namespace {
bool empty(const IRect& r) { return r.right <= r.left || r.bottom <= r.top; }
} // namespace

bool intersects(const IRect& a, const IRect& b) {
    if (empty(a) || empty(b)) return false;
    return a.left < b.right && b.left < a.right && a.top < b.bottom && b.top < a.bottom;
}

IRect toOutputRect(const IRect& screen, const IRect& out) {
    IRect r{std::max(screen.left, out.left) - out.left, std::max(screen.top, out.top) - out.top,
            std::min(screen.right, out.right) - out.left, std::min(screen.bottom, out.bottom) - out.top};
    if (empty(r)) return {};
    return r;
}

bool anyIntersects(const IRect& region, const IRect* rects, std::size_t n) {
    if (!rects) return false;
    for (std::size_t i = 0; i < n; ++i)
        if (intersects(region, rects[i])) return true;
    return false;
}

bool rotationSupported(int dxgiRotation) { return dxgiRotation == 0 || dxgiRotation == 1; }

float sdrWhiteScale(unsigned sdrWhiteLevel) { return sdrWhiteLevel ? float(sdrWhiteLevel) / 1000.0f : 1.0f; }

unsigned CaptureBackoff::nextDelayMs() {
    unsigned d = next_;
    next_ = std::min(2000u, next_ * 2);
    return d;
}

} // namespace md
