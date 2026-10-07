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

// Forme du génie à l'instant t, comme sur macOS : le bas de la fenêtre plonge d'abord jusqu'au Dock en se
// resserrant vers la case (les côtés deviennent des courbes en S), puis le haut suit dans l'entonnoir.
struct Shape {
    Local w, c;
    double p, top, bottom, span;
    // Ligne f (0 : bord le plus éloigné du Dock, 1 : le plus proche) : bornes u0, u1 et position v.
    void row(double f, double& u0, double& u1, double& v) const {
        v = lerp(top, bottom, f);
        const double bend = (span > 0 ? smooth(clamp01((v - w.v0) / span)) : 1.0) * p;
        u0 = lerp(w.u0, c.u0, bend);
        u1 = lerp(w.u1, c.u1, bend);
    }
};

Shape genieShape(const RECT& from, const RECT& to, DockPosition edge, double t) {
    Shape s{toLocal(from, edge), toLocal(to, edge), 0, 0, 0, 0};
    s.p = easeInOut(clamp01(t / 0.4));                                            // courbure
    s.bottom = lerp(s.w.v1, s.c.v1, easeInOut(clamp01(t / 0.5)));                // le bas plonge vers la case
    s.top = lerp(s.w.v0, s.c.v0, easeInOut(clamp01((t - 0.2) / 0.8)));          // le haut suit
    s.span = s.c.v0 - s.w.v0;
    return s;
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
    const Shape shape = genieShape(from, to, edge, t);
    const LONG extent = edge == DockPosition::Bottom ? src.cy : src.cx;
    const int n = std::max(1, std::min(slices, int(extent)));
    out.reserve(std::size_t(n));
    for (int k = 0; k < n; ++k) {
        const double f0 = double(k) / n, f1 = double(k + 1) / n;
        double l0, r0, a, l1, r1, b, left, right, v;
        shape.row(f0, l0, r0, a);
        shape.row(f1, l1, r1, b);
        shape.row((f0 + f1) / 2, left, right, v);   // largeur prise au milieu de la bande
        const LONG k0 = px(extent * f0), k1 = px(extent * f1);
        out.push_back({sourceStrip(src, edge, k0, k1), toScreen(px(left), px(right), px(a), px(b), edge)});
    }
    return out;
}

int genieSliceCount(long extent) { return std::clamp(int(extent / 4), 16, 128); }

std::vector<GenieVertex> genieMesh(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t,
                                   int rows) {
    std::vector<GenieVertex> out;
    if (e == MinimizeEffect::Windows || src.cx <= 0 || src.cy <= 0) return out;
    t = clamp01(t);
    if (e == MinimizeEffect::Scale) {
        const double k = easeInOut(t);
        const float l = float(lerp(from.left, to.left, k)), tp = float(lerp(from.top, to.top, k)),
                    r = float(lerp(from.right, to.right, k)), b = float(lerp(from.bottom, to.bottom, k));
        return {{l, tp, 0, 0}, {r, tp, 1, 0}, {l, b, 0, 1}, {r, b, 1, 1}};
    }
    const Shape shape = genieShape(from, to, edge, t);
    const int n = std::max(1, rows);
    out.reserve(std::size_t(n + 1) * 2);
    for (int k = 0; k <= n; ++k) {
        const double f = double(k) / n;
        double u0, u1, v;
        shape.row(f, u0, u1, v);
        const float ff = float(f);
        switch (edge) {   // repère local → écran ; texture : la ligne (ou colonne) f de la source
            case DockPosition::Left:
                out.push_back({float(-v), float(u0), 1 - ff, 0});
                out.push_back({float(-v), float(u1), 1 - ff, 1});
                break;
            case DockPosition::Right:
                out.push_back({float(v), float(u0), ff, 0});
                out.push_back({float(v), float(u1), ff, 1});
                break;
            default:
                out.push_back({float(u0), float(v), 0, ff});
                out.push_back({float(u1), float(v), 1, ff});
                break;
        }
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
