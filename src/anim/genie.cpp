#include "genie.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {

double clamp01(double x) { return std::clamp(x, 0.0, 1.0); }
double lerp(double a, double b, double t) { return a + (b - a) * t; }
double smooth(double x) { return x * x * (3 - 2 * x); }
double easeInOut(double x) { return x < 0.5 ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3) / 2; }
LONG px(double v) { return LONG(std::lround(v)); }

// Repère local : v vers le Dock, u le long du Dock.
struct Local {
    double u0, u1, v0, v1;
};

Local toLocal(const RECT& r, DockPosition edge) {
    switch (edge) {
        case DockPosition::Left: return {double(r.top), double(r.bottom), -double(r.right), -double(r.left)};
        case DockPosition::Right: return {double(r.top), double(r.bottom), double(r.left), double(r.right)};
        default: return {double(r.left), double(r.right), double(r.top), double(r.bottom)};
    }
}

RECT toScreen(LONG u0, LONG u1, LONG v0, LONG v1, DockPosition edge) {
    switch (edge) {
        case DockPosition::Left: return RECT{-v1, u0, -v0, u1};
        case DockPosition::Right: return RECT{v0, u0, v1, u1};
        default: return RECT{u0, v0, u1, v1};
    }
}

// Portion [k0, k1] (sur extent) de la source le long de v.
RECT sourceStrip(SIZE src, DockPosition edge, LONG k0, LONG k1) {
    switch (edge) {
        case DockPosition::Left: return RECT{src.cx - k1, 0, src.cx - k0, src.cy};   // v = -x : colonnes de droite à gauche
        case DockPosition::Right: return RECT{k0, 0, k1, src.cy};
        default: return RECT{0, k0, src.cx, k1};
    }
}

} // namespace

std::vector<GenieSlice> minimizeFrame(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t,
                                      int slices) {
    std::vector<GenieSlice> out;
    if (e == MinimizeEffect::Windows || src.cx <= 0 || src.cy <= 0) return out;
    t = clamp01(t);
    if (e == MinimizeEffect::Scale) {
        const double k = easeInOut(t);
        out.push_back({RECT{0, 0, src.cx, src.cy}, RECT{px(lerp(from.left, to.left, k)), px(lerp(from.top, to.top, k)),
                                                        px(lerp(from.right, to.right, k)), px(lerp(from.bottom, to.bottom, k))}});
        return out;
    }
    const Local w = toLocal(from, edge), c = toLocal(to, edge);
    const LONG extent = edge == DockPosition::Bottom ? src.cy : src.cx;
    const int n = std::max(1, std::min(slices, int(extent)));
    const double p = easeInOut(clamp01(t / 0.45));          // courbure : le bas se resserre vers la case
    const double q = easeInOut(clamp01((t - 0.2) / 0.8));   // glissement dans la case
    const double top = lerp(w.v0, c.v0, q), bottom = lerp(w.v1, c.v1, q);
    const double span = c.v0 - w.v0;
    const auto weight = [&](double v) { return span > 0 ? smooth(clamp01((v - w.v0) / span)) : 1.0; };
    out.reserve(std::size_t(n));
    for (int k = 0; k < n; ++k) {
        const double f0 = double(k) / n, f1 = double(k + 1) / n;
        const double a = lerp(top, bottom, f0), b = lerp(top, bottom, f1);
        const double bend = weight((a + b) / 2) * p;
        const double left = lerp(w.u0, c.u0, bend), right = lerp(w.u1, c.u1, bend);
        const LONG k0 = px(extent * f0), k1 = px(extent * f1);
        out.push_back({sourceStrip(src, edge, k0, k1), toScreen(px(left), px(right), px(a), px(b), edge)});
    }
    return out;
}

double minimizeDuration(MinimizeEffect e, bool slow) {
    const double base = e == MinimizeEffect::Genie ? 0.55 : e == MinimizeEffect::Scale ? 0.3 : 0.0;
    return slow ? base * 8 : base;
}

RECT restoredRect(const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor, bool toolWindow, SIZE src) {
    if (wp.flags & WPF_RESTORETOMAXIMIZED) {
        const LONG ww = work.right - work.left, wh = work.bottom - work.top;
        const LONG w = src.cx > 0 ? src.cx : ww, h = src.cy > 0 ? src.cy : wh;
        const LONG left = work.left + (ww - w) / 2, top = work.top + (wh - h) / 2;
        return RECT{left, top, left + w, top + h};
    }
    RECT r = wp.rcNormalPosition;
    if (!toolWindow) OffsetRect(&r, work.left - monitor.left, work.top - monitor.top);
    return r;
}

RECT genieStartRect(const std::optional<RECT>& lastSeen, const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor,
                    bool toolWindow, SIZE src) {
    if (lastSeen && !IsRectEmpty(&*lastSeen)) return *lastSeen;
    return restoredRect(wp, work, monitor, toolWindow, src);
}

GenieReact genieOnMinimize(const GenieRun& run, std::uint64_t window, bool minimized, bool live) {
    if (minimized) return live ? GenieReact::Start : GenieReact::Nothing;
    return run.active && run.source == window ? GenieReact::Cancel : GenieReact::Nothing;
}

bool genieMustRestoreFirst(const GenieRun& run) { return run.active && run.restoring; }

} // namespace md
