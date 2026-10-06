#include "bounce.h"

#include <cmath>

namespace md {

double launchBounceOffset(double elapsed, double period, double height) {
    if (!(period > 0) || !(elapsed > 0)) return 0;
    double p = std::fmod(elapsed, period) / period;
    return height * 4 * p * (1 - p);
}

double attentionBounceOffset(double elapsed, double period, double height, int count, double pause) {
    if (count <= 0 || !(period > 0) || !(elapsed > 0)) return 0;
    double active = period * count;
    double cycle = active + (pause > 0 ? pause : 0);
    double t = std::fmod(elapsed, cycle);
    if (t >= active) return 0;
    return launchBounceOffset(t, period, height);
}

} // namespace md
