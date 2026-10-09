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

// Forme du génie, d'après la reconstitution image par image de BCGenieEffect (B. Ciechanowski) :
//  - les côtés sont des courbes de Bézier cubiques, tangentes verticales aux deux bouts, du bord de la fenêtre le
//    plus éloigné du Dock jusqu'à la ligne d'entrée de la case ; leurs pieds se resserrent sur la largeur de la case
//    entre 0 et 40 % de la durée ;
//  - entre 30 et 100 %, le contenu glisse le long des courbes à sa taille réelle (sans étirement) et n'est écrasé
//    qu'en passant la ligne d'entrée, proportionnellement (toute la fenêtre tient dans la case à la fin) ;
//  - les deux phases suivent un smoothstep.
// Une fenêtre qui déborde déjà sous la ligne d'entrée n'est écrasée que progressivement (pas de saut à t = 0).
struct Shape {
    Local w, c;
    double line = 0, total = 0, start = 0, squeeze = 1, foot0 = 0, foot1 = 0;
    bool degenerate = false;
    double tScale = 0;

    // Ligne f de la source (0 : bord le plus éloigné du Dock, 1 : le plus proche) : bornes u0, u1 et position v.
    void row(double f, double& u0, double& u1, double& v) const {
        if (degenerate) {   // fenêtre déjà dans le Dock : simple réduction
            const double k = smooth(tScale);
            u0 = lerp(w.u0, c.u0, k);
            u1 = lerp(w.u1, c.u1, k);
            v = lerp(lerp(w.v0, c.v0, k), lerp(w.v1, c.v1, k), f);
            return;
        }
        const double y = start + f * total;
        if (y <= line) {
            const double g = clamp01((y - w.v0) / (line - w.v0));
            const double b = smooth(bezierParam(g));   // abscisse de la courbe : smoothstep du paramètre
            u0 = lerp(w.u0, foot0, b);
            u1 = lerp(w.u1, foot1, b);
            v = y;
        } else {
            u0 = foot0;
            u1 = foot1;
            v = line + (y - line) * squeeze;
        }
    }

    // Courbe (w.u, w.v0) → (pied, line), points de contrôle à mi-hauteur : v(s) = 1,5 s − 1,5 s² + s³ (normalisé),
    // strictement croissante ; on cherche s tel que v(s) = g (Newton, départ linéaire, puis bissection de secours).
    static double bezierParam(double g) {
        double s = g;
        for (int i = 0; i < 8; ++i) {
            const double f = 1.5 * s - 1.5 * s * s + s * s * s - g;
            const double df = 1.5 - 3 * s + 3 * s * s;   // ≥ 0,75
            s = clamp01(s - f / df);
        }
        return s;
    }
};

Shape genieShape(const RECT& from, const RECT& to, DockPosition edge, double t) {
    Shape s;
    s.w = toLocal(from, edge);
    s.c = toLocal(to, edge);
    s.line = s.c.v0;
    s.total = s.w.v1 - s.w.v0;
    if (s.line - s.w.v0 < 1 || s.total <= 0) {
        s.degenerate = true;
        s.tScale = clamp01(t);
        return s;
    }
    const double curve = smooth(clamp01(t / 0.4));            // courbes : 0 → 40 %
    const double slide = smooth(clamp01((t - 0.3) / 0.7));    // glissement : 30 → 100 %
    s.foot0 = lerp(s.w.u0, s.c.u0, curve);
    s.foot1 = lerp(s.w.u1, s.c.u1, curve);
    s.start = lerp(s.w.v0, s.line, slide);
    s.squeeze = lerp(1.0, (s.c.v1 - s.c.v0) / s.total, curve);
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

GenieWait genieWaitStep(bool gpuReady, double waited) {
    if (gpuReady) return GenieWait::Go;
    return waited < 0.15 ? GenieWait::Hold : GenieWait::GoStrips;
}

int genieStripTarget(int full, bool waiting, int have) {
    if (have <= 0 || waiting) return 1;
    return full;
}

bool genieMinimizeConfirmed(POINT down, POINT up) {
    return std::abs(up.x - down.x) <= 4 && std::abs(up.y - down.y) <= 4;
}

bool genieOnMinimizeButton(POINT pt, const RECT& window, const RECT& buttonBounds, bool hasMinimizeBox) {
    const LONG width = buttonBounds.right - buttonBounds.left;
    if (!hasMinimizeBox || width <= 0 || buttonBounds.bottom <= buttonBounds.top) return false;
    const LONG left = window.left + buttonBounds.left, top = window.top + buttonBounds.top;
    return pt.x >= left && pt.x < left + width / 3 && pt.y >= top && pt.y < window.top + buttonBounds.bottom;
}

RECT genieHostBox(const RECT& windowMonitor, const RECT& dockMonitor) {
    RECT box{};
    UnionRect(&box, &windowMonitor, &dockMonitor);
    return box;
}

RECT genieGpuBox(const RECT& from, const RECT& to, const RECT* armed) {
    RECT box{};
    UnionRect(&box, &from, &to);
    InflateRect(&box, 2, 2);
    if (armed && !IsRectEmpty(armed) && armed->left <= box.left && armed->top <= box.top && armed->right >= box.right &&
        armed->bottom >= box.bottom)
        return *armed;
    return box;
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
    // macOS 26 Tahoe (Golden Gate les raccourcissait de ~12 % : 0,48 s et 0,26 s).
    const double base = e == MinimizeEffect::Genie ? 0.55 : e == MinimizeEffect::Scale ? 0.30 : 0.0;
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

RECT genieVisibleRect(const RECT& window, SIZE thumb) {
    const LONG dx = (window.right - window.left) - thumb.cx, dy = (window.bottom - window.top) - thumb.cy;
    if (dx < 0 || dy < 0 || dx > 64 || dy > 64 || (dx == 0 && dy == 0)) return window;
    const LONG left = window.left + dx / 2;
    return RECT{left, window.top, left + thumb.cx, window.top + thumb.cy};
}

RECT thumbnailFrame(const RECT& window, const std::optional<RECT>& frame, SIZE thumb) {
    if (thumb.cx <= 0 || thumb.cy <= 0) return window;
    if (frame && std::abs((frame->right - frame->left) - thumb.cx) <= 1 && std::abs((frame->bottom - frame->top) - thumb.cy) <= 1)
        return *frame;
    return genieVisibleRect(window, thumb);
}

RECT genieStartRect(const std::optional<RECT>& lastSeen, const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor,
                    bool toolWindow, SIZE src) {
    if (lastSeen && !IsRectEmpty(&*lastSeen)) return *lastSeen;
    return restoredRect(wp, work, monitor, toolWindow, src);
}

GenieReact genieOnMinimize(const GenieRun& run, std::uint64_t window, bool minimized, bool live) {
    if (minimized) return live ? GenieReact::Start : GenieReact::Nothing;
    // Restaurée ailleurs pendant son animation : annulée ; pendant la fin d'une ouverture, c'est notre restauration.
    return run.active && !run.settling && run.source == window ? GenieReact::Cancel : GenieReact::Nothing;
}

MinimizeAnnounce minimizeAnnounce(bool hide, MinimizeEffect effect, bool minimizesToTile) {
    if (hide) return {true, true};
    return {effect != MinimizeEffect::Windows && minimizesToTile, false};
}

bool genieTakesMinimize(bool held, bool windowsAnimates) { return held || !windowsAnimates; }

bool genieMustRestoreFirst(const GenieRun& run) { return run.active && run.restoring && !run.settling; }

} // namespace md
