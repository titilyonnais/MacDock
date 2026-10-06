#include "capture_policy.h"

#include <algorithm>
#include <cstring>

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

bool permanentCaptureFailure(long hr, bool rotationUnsupported) {
    constexpr long kDxgiUnsupported = long(0x887A0004);
    return rotationUnsupported || hr == kDxgiUnsupported;
}

float sdrWhiteScale(unsigned sdrWhiteLevel) { return sdrWhiteLevel ? float(sdrWhiteLevel) / 1000.0f : 1.0f; }

unsigned CaptureBackoff::nextDelayMs() {
    unsigned d = next_;
    next_ = std::min(2000u, next_ * 2);
    return d;
}

bool ChangeGate::changed(const std::uint8_t* pixels, unsigned w, unsigned h, unsigned rowPitch) {
    const std::size_t row = std::size_t(w) * 4;
    bool diff = w != w_ || h != h_ || last_.size() != row * h;
    if (diff) {
        last_.assign(row * h, 0);
        w_ = w;
        h_ = h;
    }
    for (unsigned y = 0; y < h; ++y) {
        std::uint8_t* dst = &last_[row * y];
        const std::uint8_t* src = pixels + std::size_t(rowPitch) * y;
        if (!diff && std::memcmp(dst, src, row) != 0) diff = true;
        if (diff) std::memcpy(dst, src, row);
    }
    return diff;
}

} // namespace md
