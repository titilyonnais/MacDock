#include "icon_grid.h"

#include <algorithm>
#include <cmath>

namespace md {

int iconShapePx(int tilePx, double shapeRatio) {
    double s = double(tilePx) * shapeRatio;
    if (!std::isfinite(s)) return 1;
    return std::max(1, int(std::lround(s)));
}

std::vector<std::uint8_t> placeOnGrid(const std::vector<std::uint8_t>& content, int shape, int tile) {
    std::vector<std::uint8_t> out(size_t(std::max(0, tile)) * std::max(0, tile) * 4, 0);
    if (shape <= 0 || tile <= 0 || content.size() < size_t(shape) * shape * 4) return out;
    const int off = (tile - shape) / 2;
    for (int y = 0; y < shape; ++y) {
        int dy = y + off;
        if (dy < 0 || dy >= tile) continue;
        for (int x = 0; x < shape; ++x) {
            int dx = x + off;
            if (dx < 0 || dx >= tile) continue;
            std::copy_n(&content[(size_t(y) * shape + x) * 4], 4, &out[(size_t(dy) * tile + dx) * 4]);
        }
    }
    return out;
}

void addDropShadow(std::vector<std::uint8_t>& bgra, int size, double sigmaPx, double offsetYPx, double opacity) {
    if (!(opacity > 0) || size <= 0 || bgra.size() < size_t(size) * size * 4) return;
    opacity = std::min(opacity, 1.0);
    const size_t n = size_t(size) * size;

    // Alpha décalé vers le bas (arrondi au pixel), puis flou gaussien séparable.
    const int dy = int(std::lround(std::isfinite(offsetYPx) ? offsetYPx : 0.0));
    std::vector<float> a(n, 0.0f), tmp(n, 0.0f);
    for (int y = 0; y < size; ++y) {
        int sy = y - dy;
        if (sy < 0 || sy >= size) continue;
        for (int x = 0; x < size; ++x) a[size_t(y) * size + x] = bgra[(size_t(sy) * size + x) * 4 + 3] / 255.0f;
    }
    const double sigma = std::isfinite(sigmaPx) ? std::max(0.0, sigmaPx) : 0.0;
    const int radius = int(std::ceil(3 * sigma));
    if (radius > 0) {
        std::vector<float> k(size_t(2 * radius + 1));
        float sum = 0;
        for (int i = -radius; i <= radius; ++i) sum += k[size_t(i + radius)] = float(std::exp(-i * i / (2 * sigma * sigma)));
        for (auto& v : k) v /= sum;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                float acc = 0;
                for (int i = -radius; i <= radius; ++i) {
                    int sx = x + i;
                    if (sx >= 0 && sx < size) acc += k[size_t(i + radius)] * a[size_t(y) * size + sx];
                }
                tmp[size_t(y) * size + x] = acc;
            }
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                float acc = 0;
                for (int i = -radius; i <= radius; ++i) {
                    int sy = y + i;
                    if (sy >= 0 && sy < size) acc += k[size_t(i + radius)] * tmp[size_t(sy) * size + x];
                }
                a[size_t(y) * size + x] = acc;
            }
    }

    // Image par-dessus l'ombre noire (prémultiplié) : out = src + shadow·(1 − αsrc).
    for (size_t i = 0; i < n; ++i) {
        float shadow = float(opacity) * a[i];
        if (shadow <= 0) continue;
        auto* p = &bgra[i * 4];
        float inv = 1.0f - p[3] / 255.0f;
        p[3] = std::uint8_t(std::lround(std::min(255.0f, p[3] + shadow * inv * 255.0f)));
    }
}

} // namespace md
