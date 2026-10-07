#include "vector_art.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace md {

namespace {

bool inside(const Poly& p, double x, double y) {
    bool in = false;
    const auto& v = p.pts;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
        if ((v[i].second > y) != (v[j].second > y) &&
            x < (v[j].first - v[i].first) * (y - v[i].second) / (v[j].second - v[i].second) + v[i].first)
            in = !in;
    }
    return in;
}

double edgeDistance(const Poly& p, double x, double y) {
    double best = 1e18;
    const auto& v = p.pts;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
        const double ax = v[j].first, ay = v[j].second, bx = v[i].first, by = v[i].second;
        const double vx = bx - ax, vy = by - ay, len = vx * vx + vy * vy;
        const double t = len > 0 ? std::clamp(((x - ax) * vx + (y - ay) * vy) / len, 0.0, 1.0) : 0.0;
        const double dx = x - (ax + t * vx), dy = y - (ay + t * vy);
        best = std::min(best, dx * dx + dy * dy);
    }
    return std::sqrt(best);
}

struct Box {
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
};

struct Premul {
    double a = 0, r = 0, g = 0, b = 0;
    void over(std::uint32_t argb, double k = 1) {
        const double sa = ((argb >> 24) & 0xFF) / 255.0 * k;
        r = ((argb >> 16) & 0xFF) * sa + r * (1 - sa);
        g = ((argb >> 8) & 0xFF) * sa + g * (1 - sa);
        b = (argb & 0xFF) * sa + b * (1 - sa);
        a = sa + a * (1 - sa);
    }
};

} // namespace

BgraImage rasterize(const std::vector<Layer>& layers, int size, double unit, double shadow) {
    BgraImage im{size, size, std::vector<std::uint8_t>(std::size_t(size) * size * 4, 0)};
    std::vector<Box> boxes;
    for (const Layer& l : layers) {
        Box b;
        for (const Poly& p : l.shapes)
            for (auto [x, y] : p.pts) {
                b.x0 = std::min(b.x0, x - l.outlineWidth);
                b.y0 = std::min(b.y0, y - l.outlineWidth);
                b.x1 = std::max(b.x1, x + l.outlineWidth);
                b.y1 = std::max(b.y1, y + l.outlineWidth);
            }
        boxes.push_back(b);
    }
    std::vector<Premul> acc(std::size_t(size) * size);
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            Premul sum;
            for (int sy = 0; sy < 4; ++sy)
                for (int sx = 0; sx < 4; ++sx) {
                    const double x = (px + (sx + 0.5) / 4) / unit, y = (py + (sy + 0.5) / 4) / unit;
                    Premul s;
                    for (std::size_t li = 0; li < layers.size(); ++li) {
                        const Box& b = boxes[li];
                        if (x < b.x0 || x > b.x1 || y < b.y0 || y > b.y1) continue;
                        const Layer& l = layers[li];
                        bool in = false;
                        for (const Poly& p : l.shapes) in = in || inside(p, x, y);
                        if (in) {
                            s.over(l.fill);
                            continue;
                        }
                        if (l.outlineWidth <= 0) continue;
                        double d = 1e18;
                        for (const Poly& p : l.shapes) d = std::min(d, edgeDistance(p, x, y));
                        if (d <= l.outlineWidth) s.over(l.outline);
                    }
                    sum.a += s.a / 16;
                    sum.r += s.r / 16;
                    sum.g += s.g / 16;
                    sum.b += s.b / 16;
                }
            acc[std::size_t(py) * size + px] = sum;
        }
    if (shadow > 0) {   // ombre : alpha décalé vers le bas et flouté (boîte), sous l'image
        const int dy = std::max(1, int(std::lround(0.6 * unit))), r = std::max(1, int(std::lround(0.8 * unit)));
        std::vector<double> a(acc.size(), 0);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                double s = 0;
                int n = 0;
                for (int j = -r; j <= r; ++j)
                    for (int i = -r; i <= r; ++i) {
                        const int sx = x + i, sy = y + j - dy;
                        ++n;
                        if (sx >= 0 && sy >= 0 && sx < size && sy < size) s += acc[std::size_t(sy) * size + sx].a;
                    }
                a[std::size_t(y) * size + x] = s / n * 0.3 * shadow;
            }
        for (std::size_t i = 0; i < acc.size(); ++i) {   // image par-dessus l'ombre noire
            Premul& p = acc[i];
            p.a = p.a + a[i] * (1 - p.a);
        }
    }
    for (std::size_t i = 0; i < acc.size(); ++i) {
        const Premul& p = acc[i];
        std::uint8_t* o = &im.px[i * 4];
        if (p.a <= 0) continue;
        o[0] = std::uint8_t(std::lround(std::clamp(p.b / p.a, 0.0, 255.0)));
        o[1] = std::uint8_t(std::lround(std::clamp(p.g / p.a, 0.0, 255.0)));
        o[2] = std::uint8_t(std::lround(std::clamp(p.r / p.a, 0.0, 255.0)));
        o[3] = std::uint8_t(std::lround(std::clamp(p.a * 255, 0.0, 255.0)));
    }
    return im;
}

Poly rotated(const Poly& p, double degrees, double cx, double cy) {
    const double a = degrees * std::numbers::pi / 180, c = std::cos(a), s = std::sin(a);
    Poly out;
    for (auto [x, y] : p.pts) out.pts.push_back({cx + (x - cx) * c - (y - cy) * s, cy + (x - cx) * s + (y - cy) * c});
    return out;
}

Poly translated(const Poly& p, double dx, double dy) {
    Poly out;
    for (auto [x, y] : p.pts) out.pts.push_back({x + dx, y + dy});
    return out;
}

Poly circle(double cx, double cy, double r, int steps) {
    Poly out;
    for (int i = 0; i < steps; ++i) {
        const double a = 2 * std::numbers::pi * i / steps;
        out.pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
    }
    return out;
}

Poly ring(double cx, double cy, double inner, double outer, int steps) {
    Poly out = circle(cx, cy, outer, steps);
    out.pts.push_back(out.pts.front());   // le pont aller-retour s'annule en pair-impair
    const Poly in = circle(cx, cy, inner, steps);
    for (auto it = in.pts.begin(); it != in.pts.end(); ++it) out.pts.push_back(*it);
    out.pts.push_back(in.pts.front());
    return out;
}

Poly rect(double x0, double y0, double x1, double y1) { return Poly{{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}}; }

} // namespace md
