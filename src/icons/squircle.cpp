#include "squircle.h"

#include <cmath>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

#include "../geom/smooth_rect.h"

namespace md {

namespace {
const std::vector<float>& maskFor(int size, double cornerRatio) {
    static std::mutex lock;
    static std::map<std::pair<int, double>, std::vector<float>> cache;
    std::lock_guard guard(lock);
    auto& m = cache[{size, cornerRatio}];
    if (m.empty()) m = smoothSquareMask(size, cornerRatio);
    return m;
}
} // namespace

bool insideSquircle(double x, double y, double size, double cornerRatio) {
    if (!(size > 0) || x < 0 || y < 0 || x > size || y > size) return false;
    double rl = limitedCornerRadius(size, size, cornerRatio * size);
    double e = kCornerExtent * rl;
    if ((x >= e && x <= size - e) || (y >= e && y <= size - e)) return true;
    return pointInPolygon(smoothRectOutline(0, 0, size, size, rl, 24), x, y);
}

double squircleMaskAlpha(int px, int py, int size, double cornerRatio) {
    if (size <= 0 || px < 0 || py < 0 || px >= size || py >= size) return 0;
    return maskFor(size, cornerRatio)[size_t(py) * size + px];
}

double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride) {
    if (!bgra || w <= 0 || h <= 0) return 0;
    const int size = w < h ? w : h;
    const auto& mask = maskFor(size, kIconCornerRatio);
    long inside = 0, opaque = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            if (mask[size_t(y) * size + x] < 0.5f) continue;
            ++inside;
            if (bgra[y * stride + x * 4 + 3] > 200) ++opaque;
        }
    return inside ? double(opaque) / double(inside) : 0;
}

bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride) {
    return squircleCoverage(bgra, w, h, stride) >= 0.88;
}

} // namespace md
