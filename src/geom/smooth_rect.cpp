#include "smooth_rect.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

// Demi-coin d'Apple (chemin rétro-conçu de UIKit), en unités de rayon, symétrisé autour de la diagonale.
// X : distance au sommet le long du bord horizontal ; Y : distance au sommet le long du bord vertical.
struct Seg {
    bool curve;
    Pt c1, c2, end;
};
const Seg kCorner[] = {
    {true, {1.08849323, 0}, {0.86840689, 0}, {0.66993427, 0.06549600}},
    {false, {}, {}, {0.63149399, 0.07491100}},
    {true, {0.37282392, 0.16905899}, {0.16905899, 0.37282392}, {0.07491100, 0.63149399}},
    {false, {}, {}, {0.06549600, 0.66993427}},
    {true, {0, 0.86840689}, {0, 1.08849323}, {0, kCornerExtent}},
};

Pt bezier(Pt p0, Pt c1, Pt c2, Pt p1, double t) {
    double u = 1 - t;
    double a = u * u * u, b = 3 * u * u * t, c = 3 * u * t * t, d = t * t * t;
    return {a * p0.x + b * c1.x + c * c2.x + d * p1.x, a * p0.y + b * c1.y + c * c2.y + d * p1.y};
}

// Courbe d'un coin en coordonnées du coin (X, Y), de (kCornerExtent, 0) à (0, kCornerExtent).
std::vector<Pt> cornerCurve(int steps) {
    steps = std::max(1, steps);
    std::vector<Pt> pts{{kCornerExtent, 0}};
    Pt cur = pts.front();
    for (const Seg& s : kCorner) {
        if (!s.curve) {
            pts.push_back(s.end);
        } else {
            for (int k = 1; k <= steps; ++k) pts.push_back(bezier(cur, s.c1, s.c2, s.end, double(k) / steps));
        }
        cur = s.end;
    }
    return pts;
}

double segmentDistance(Pt p, Pt a, Pt b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / len2, 0.0, 1.0) : 0.0;
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

} // namespace

double limitedCornerRadius(double w, double h, double r) {
    if (!(w > 0) || !(h > 0) || !(r > 0)) return 0;
    double lim = std::min(w, h) / 2 / kCornerExtent;
    double out = std::min(r, lim);
    return std::isfinite(out) ? out : 0;
}

std::vector<Pt> smoothRectOutline(double x, double y, double w, double h, double r, int stepsPerCurve) {
    w = std::max(0.0, w);
    h = std::max(0.0, h);
    const double rl = limitedCornerRadius(w, h, r);
    const double R = x + w, B = y + h;
    if (rl <= 0) return {{x, y}, {R, y}, {R, B}, {x, B}};

    const std::vector<Pt> c = cornerCurve(stepsPerCurve);
    std::vector<Pt> out;
    out.reserve(c.size() * 4);
    for (const Pt& q : c) out.push_back({R - q.x * rl, y + q.y * rl});   // haut-droit
    for (const Pt& q : c) out.push_back({R - q.y * rl, B - q.x * rl});   // bas-droit
    for (const Pt& q : c) out.push_back({x + q.x * rl, B - q.y * rl});   // bas-gauche
    for (const Pt& q : c) out.push_back({x + q.y * rl, y + q.x * rl});   // haut-gauche
    // Le premier point doit être le début du bord haut (x + extent·rl, y) : rotation du dernier coin.
    std::rotate(out.begin(), out.end() - 1, out.end());
    return out;
}

bool pointInPolygon(const std::vector<Pt>& poly, double x, double y) {
    bool inside = false;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        const Pt& a = poly[i];
        const Pt& b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) inside = !inside;
    }
    return inside;
}

double polygonArea(const std::vector<Pt>& poly) {
    double s = 0;
    const size_t n = poly.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) s += poly[j].x * poly[i].y - poly[i].x * poly[j].y;
    return std::fabs(s) / 2;
}

std::vector<float> smoothSquareMask(int size, double cornerRatio) {
    if (size <= 0) return {};
    std::vector<float> m(size_t(size) * size, 0.0f);
    const double rl = limitedCornerRadius(size, size, cornerRatio * size);
    const double e = kCornerExtent * rl;
    const auto poly = smoothRectOutline(0, 0, size, size, rl, 24);
    for (int py = 0; py < size; ++py)
        for (int px = 0; px < size; ++px) {
            int inside = 0;
            for (int j = 0; j < 4; ++j)
                for (int i = 0; i < 4; ++i) {
                    double sx = px + (i + 0.5) / 4.0, sy = py + (j + 0.5) / 4.0;
                    bool straight = (sx >= e && sx <= size - e) || (sy >= e && sy <= size - e);
                    inside += straight ? 1 : pointInPolygon(poly, sx, sy);
                }
            m[size_t(py) * size + px] = inside / 16.0f;
        }
    return m;
}

std::vector<float> cornerDistanceField(int n, double lo, double hi) {
    if (n <= 0 || !(hi > lo)) return {};
    std::vector<Pt> line{{hi + 10, 0}};
    for (const Pt& p : cornerCurve(32)) line.push_back(p);
    line.push_back({0, hi + 10});
    std::vector<Pt> region = line;
    region.push_back({hi + 10, hi + 10});

    std::vector<float> f(size_t(n) * n);
    const double cell = (hi - lo) / n;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            Pt p{lo + (i + 0.5) * cell, lo + (j + 0.5) * cell};
            double d = 1e30;
            for (size_t k = 0; k + 1 < line.size(); ++k) d = std::min(d, segmentDistance(p, line[k], line[k + 1]));
            bool inside = p.x > 0 && p.y > 0 && pointInPolygon(region, p.x, p.y);
            f[size_t(j) * n + i] = float(inside ? -d : d);
        }
    return f;
}

double sampleCornerField(const std::vector<float>& f, int n, double lo, double hi, double X, double Y) {
    if (n <= 0 || f.size() < size_t(n) * n || !(hi > lo)) return 0;
    const double cell = (hi - lo) / n;
    double u = std::clamp((X - lo) / cell - 0.5, 0.0, double(n - 1));
    double v = std::clamp((Y - lo) / cell - 0.5, 0.0, double(n - 1));
    int i0 = int(std::floor(u)), j0 = int(std::floor(v));
    int i1 = std::min(i0 + 1, n - 1), j1 = std::min(j0 + 1, n - 1);
    double fu = u - i0, fv = v - j0;
    auto at = [&](int i, int j) { return double(f[size_t(j) * n + i]); };
    return (at(i0, j0) * (1 - fu) + at(i1, j0) * fu) * (1 - fv) + (at(i0, j1) * (1 - fu) + at(i1, j1) * fu) * fv;
}

} // namespace md
