#include "hud_logic.h"

#include <algorithm>
#include <cmath>

namespace md {

float volumeStep(float v, int dir, bool fine) {
    const double g = fine ? 1.0 / 64 : 1.0 / 16;
    const double x = std::clamp(double(v), 0.0, 1.0) / g;
    const double n = dir > 0 ? std::floor(x + 1e-3) + 1 : std::ceil(x - 1e-3) - 1;
    return float(std::clamp(n * g, 0.0, 1.0));
}

float HudFade::opacity(double now) const {
    if (!shown_) return 0;
    const double t = now - last_ - kHold;
    if (t <= 0) return 1;
    return float(std::max(0.0, 1 - t / kFade));
}

HudPlace hudPlace(const RECT& monitor, int barBottom, float scale) {
    HudPlace p;
    p.w = int(std::lround(280 * scale));
    p.h = int(std::lround(64 * scale));
    p.x = monitor.right - int(std::lround(12 * scale)) - p.w;
    p.y = barBottom + int(std::lround(8 * scale));
    return p;
}

bool BrightnessGate::accept(double now, bool menuOpen) {
    if (!armed_) {
        armed_ = true;
        return false;
    }
    if (menuOpen) return false;
    return !(ownSet_ && now - own_ < 1.0);
}

} // namespace md
