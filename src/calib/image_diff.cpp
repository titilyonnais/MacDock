#include "image_diff.h"

#include <algorithm>
#include <cstdlib>

namespace md {

DiffStats diffImages(const std::uint8_t* a, const std::uint8_t* b, int w, int h, int threshold) {
    DiffStats s;
    if (!a || !b || w <= 0 || h <= 0) return s;
    s.sameSize = true;
    const size_t n = size_t(w) * h;
    double sum = 0;
    size_t above = 0;
    for (size_t i = 0; i < n; ++i) {
        int worst = 0;
        for (int c = 0; c < 3; ++c) {
            int d = std::abs(int(a[i * 4 + c]) - int(b[i * 4 + c]));
            sum += d;
            worst = std::max(worst, d);
        }
        s.maxAbs = std::max(s.maxAbs, worst);
        if (worst > threshold) ++above;
    }
    s.meanAbs = sum / double(n * 3);
    s.fractionAbove = double(above) / double(n);
    return s;
}

std::vector<std::uint8_t> diffHeatmap(const std::uint8_t* a, const std::uint8_t* b, int w, int h) {
    if (!a || !b || w <= 0 || h <= 0) return {};
    const size_t n = size_t(w) * h;
    std::vector<std::uint8_t> out(n * 4);
    for (size_t i = 0; i < n; ++i) {
        int la = a[i * 4] + a[i * 4 + 1] + a[i * 4 + 2];
        int lb = b[i * 4] + b[i * 4 + 1] + b[i * 4 + 2];
        int d = std::clamp((la - lb) / 3, -127, 127);   // > 0 : a plus clair
        auto* p = &out[i * 4];
        int gray = 128 - std::abs(d) / 2;
        p[0] = std::uint8_t(gray + (d < 0 ? -d : 0));   // bleu : b plus clair
        p[1] = std::uint8_t(gray);
        p[2] = std::uint8_t(gray + (d > 0 ? d : 0));    // rouge : a plus clair
        p[3] = 255;
    }
    return out;
}

} // namespace md
