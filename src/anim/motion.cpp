#include "motion.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

SpringParams springFromResponse(double response, double dampingFraction) {
    const double r = std::max(response, 1e-3);
    return {std::pow(2 * kPi / r, 2), 4 * kPi * std::max(dampingFraction, 0.0) / r};
}

double CubicBezier::operator()(double x) const {
    x = std::clamp(x, 0.0, 1.0);
    if (x <= 0) return 0;
    if (x >= 1) return 1;
    auto bx = [&](double t) { return 3 * (1 - t) * (1 - t) * t * x1 + 3 * (1 - t) * t * t * x2 + t * t * t; };
    auto by = [&](double t) { return 3 * (1 - t) * (1 - t) * t * y1 + 3 * (1 - t) * t * t * y2 + t * t * t; };
    auto dx = [&](double t) { return 3 * (1 - t) * (1 - t) * x1 + 6 * (1 - t) * t * (x2 - x1) + 3 * t * t * (1 - x2); };
    // Newton d'abord (rapide), dichotomie si la pente s'annule (sûre : x croît avec t pour x1, x2 dans [0, 1]).
    double t = x;
    for (int i = 0; i < 8; ++i) {
        const double err = bx(t) - x, d = dx(t);
        if (std::abs(err) < 1e-7) return by(t);
        if (std::abs(d) < 1e-6) break;
        t = std::clamp(t - err / d, 0.0, 1.0);
    }
    double lo = 0, hi = 1;
    t = x;
    for (int i = 0; i < 40; ++i) {
        const double v = bx(t);
        if (std::abs(v - x) < 1e-7) break;
        (v < x ? lo : hi) = t;
        t = (lo + hi) / 2;
    }
    return by(t);
}

MotionPreset motionPreset(Motion m) {
    MotionPreset p;
    switch (m) {   // mesuré sur macOS : apparitions vives, fermetures plus vives encore, rebonds légers
        case Motion::MenuOpen: p = {0.18, kEaseOut, 0.25, 1.0, {}}; break;
        case Motion::MenuClose: p = {0.12, kEaseIn, 0.2, 1.0, {}}; break;
        case Motion::DockMagnify: p = {0, kDefaultTiming, 0.18, 0.9, {}}; break;
        case Motion::WindowBounce: p = {0, kDefaultTiming, 0.35, 0.55, {}}; break;
        case Motion::PanelSlide: p = {0.30, kDefaultTiming, 0.35, 0.9, {}}; break;
        case Motion::GlassMorph: p = {0, kDefaultTiming, 0.42, 0.8, {}}; break;
    }
    p.spring = springFromResponse(p.response, p.dampingFraction);
    return p;
}

GlassMorph glassEmerge(double edgeY, const GlassMorph& to, double t, double reach) {
    if (t >= 1) return {to.left, to.top, to.right, to.bottom, 0};
    // Goutte de départ : petite pilule centrée sous la forme, posée un peu dans le bord (fondue avec le verre voisin).
    const double h = to.bottom - to.top, w = to.right - to.left, cx = (to.left + to.right) / 2;
    const double seedH = h * 0.45, seedW = std::max(seedH, w * 0.35);
    const GlassMorph seed{cx - seedW / 2, edgeY + seedH * 0.25 - seedH, cx + seedW / 2, edgeY + seedH * 0.25, reach};
    const double e = kEaseOut(std::clamp(t, 0.0, 1.0));
    auto mix = [&](double a, double b) { return a + (b - a) * e; };
    return {mix(seed.left, to.left), mix(seed.top, to.top), mix(seed.right, to.right), mix(seed.bottom, to.bottom),
            reach * (1 - e)};
}

} // namespace md
