#include "squircle.h"

#include <cmath>

namespace md {

bool insideSquircle(double x, double y, double size, double exponent) {
    if (!(size > 0)) return false;
    double u = std::fabs(2.0 * x / size - 1.0);
    double v = std::fabs(2.0 * y / size - 1.0);
    return std::pow(u, exponent) + std::pow(v, exponent) <= 1.0;
}

double squircleMaskAlpha(int px, int py, int size, double exponent) {
    if (size <= 0) return 0;
    int inside = 0;
    for (int j = 0; j < 4; ++j)
        for (int i = 0; i < 4; ++i)
            inside += insideSquircle(px + (i + 0.5) / 4.0, py + (j + 0.5) / 4.0, size, exponent);
    return inside / 16.0;
}

double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride) {
    if (!bgra || w <= 0 || h <= 0) return 0;
    long inside = 0, opaque = 0;
    double size = w < h ? w : h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!insideSquircle(x + 0.5, y + 0.5, size)) continue;
            ++inside;
            if (bgra[y * stride + x * 4 + 3] > 200) ++opaque;
        }
    return inside ? double(opaque) / double(inside) : 0;
}

bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride) {
    return squircleCoverage(bgra, w, h, stride) >= 0.88;
}

} // namespace md
