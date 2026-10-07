#include "wallpaper_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace md {

namespace {

struct Rgb { double r, g, b; };
Rgb mix(Rgb a, Rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

struct Wave {
    double base, amp, freq, phase, amp2, freq2;
    Rgb color;
    double alpha;
};

} // namespace

BgraImage tahoeWallpaper(int w, int h, bool dark) {
    BgraImage im{w, h, std::vector<std::uint8_t>(std::size_t(std::max(w, 0)) * std::max(h, 0) * 4, 0)};
    if (w <= 0 || h <= 0) return im;
    const Rgb top = dark ? Rgb{10, 22, 58} : Rgb{150, 205, 255}, bottom = dark ? Rgb{4, 6, 20} : Rgb{40, 100, 235};
    const Wave waves[] = {
        {0.42, 0.07, 0.8, 0.3, 0.025, 2.3, dark ? Rgb{20, 70, 160} : Rgb{90, 200, 250}, 0.55},
        {0.55, 0.09, 0.6, 1.9, 0.03, 1.7, dark ? Rgb{40, 40, 140} : Rgb{0, 122, 255}, 0.6},
        {0.68, 0.06, 1.1, 4.0, 0.02, 2.9, dark ? Rgb{70, 30, 120} : Rgb{88, 86, 214}, 0.55},
        {0.82, 0.05, 0.9, 2.6, 0.02, 3.3, dark ? Rgb{25, 15, 60} : Rgb{175, 82, 222}, 0.5},
    };
    const double aspect = double(w) / h;
    std::vector<std::vector<double>> edge(std::size(waves), std::vector<double>(std::size_t(w)));
    for (std::size_t k = 0; k < std::size(waves); ++k)
        for (int x = 0; x < w; ++x) {   // une ligne d'onde par vague, calculée une fois par colonne
            const double u = double(x) / w * aspect;
            const Wave& wv = waves[k];
            edge[k][std::size_t(x)] = wv.base + wv.amp * std::sin(2 * std::numbers::pi * wv.freq * u / aspect * 1.6 + wv.phase) +
                                      wv.amp2 * std::sin(2 * std::numbers::pi * wv.freq2 * u / aspect + wv.phase * 1.7);
        }
    const double soft = 0.035, rimWidth = 0.006;
    std::uint32_t seed = 0x9E3779B9u;
    for (int y = 0; y < h; ++y) {
        const double v = double(y) / h;
        const Rgb back = mix(top, bottom, smoothstep(0, 1, v));
        for (int x = 0; x < w; ++x) {
            Rgb c = back;
            double rim = 0;
            for (std::size_t k = 0; k < std::size(waves); ++k) {
                const double e = edge[k][std::size_t(x)];
                const double cover = smoothstep(e - soft, e + soft, v) * waves[k].alpha;   // sous la ligne : la vague
                const Rgb deep = mix(waves[k].color, bottom, smoothstep(e, e + 0.5, v) * 0.6);
                c = mix(c, deep, cover);
                const double d = (v - e) / rimWidth;
                rim += std::exp(-d * d) * (dark ? 0.12 : 0.22);   // liseré clair, comme du verre
            }
            c = mix(c, Rgb{255, 255, 255}, std::min(rim, 0.5));
            seed = seed * 1664525u + 1013904223u;   // tramage léger contre les bandes
            const double dither = ((seed >> 8) & 0xFF) / 255.0 - 0.5;
            std::uint8_t* p = &im.px[(std::size_t(y) * w + x) * 4];
            p[0] = std::uint8_t(std::clamp(std::lround(c.b + dither), 0L, 255L));
            p[1] = std::uint8_t(std::clamp(std::lround(c.g + dither), 0L, 255L));
            p[2] = std::uint8_t(std::clamp(std::lround(c.r + dither), 0L, 255L));
            p[3] = 255;
        }
    }
    return im;
}

} // namespace md
