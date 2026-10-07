#include "traffic_lights.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace md {

namespace {
constexpr const wchar_t* kShellClasses[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW"};
constexpr double kDiameter = 12, kSpacing = 20, kFirst = 20, kTail = 8, kMinTitle = 20, kDefaultTitle = 28;   // points
}

bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT dpi) {
    if (mode == LightsMode::Off || w.iconic || w.ownProcess) return false;
    if ((w.style & WS_CAPTION) != WS_CAPTION || !(w.style & WS_SYSMENU) || (w.style & WS_CHILD)) return false;
    if (w.exStyle & WS_EX_TOOLWINDOW) return false;
    for (const wchar_t* c : kShellClasses)
        if (w.className == c) return false;
    if (mode == LightsMode::Standard && w.client.top - w.frame.top < std::lround(kMinTitle * dpi / 96.0)) return false;
    return true;
}

LightsLayout lightsLayout(const RECT& frame, const RECT& client, UINT dpi) {
    const double k = (dpi ? dpi : 96) / 96.0;
    LightsLayout l;
    LONG titleH = client.top - frame.top;
    if (titleH < std::lround(kMinTitle * k)) titleH = std::lround(kDefaultTitle * k);
    const LONG cy = frame.top + titleH / 2;
    const LONG r = std::lround(kDiameter / 2 * k);
    l.radius = double(r);
    for (int i = 0; i < 3; ++i) {
        const LONG cx = frame.left + std::lround((kFirst + kSpacing * i) * k);
        l.circles[i] = RECT{cx - r, cy - r, cx + r, cy + r};
    }
    const LONG lastCx = (l.circles[2].left + l.circles[2].right) / 2;
    l.window = RECT{frame.left + 4, frame.top, lastCx + std::lround((kDiameter / 2 + kTail) * k), frame.top + titleH};
    l.window.right = std::min(l.window.right, frame.right);
    l.window.bottom = std::min(l.window.bottom, frame.bottom);
    l.patch = l.window;
    return l;
}

int hitLight(const LightsLayout& l, POINT p) {
    for (int i = 0; i < 3; ++i) {
        const double cx = (l.circles[i].left + l.circles[i].right) / 2.0, cy = (l.circles[i].top + l.circles[i].bottom) / 2.0;
        const double dx = p.x - cx, dy = p.y - cy;
        if (dx * dx + dy * dy <= (l.radius + 2) * (l.radius + 2)) return i;
    }
    return -1;
}

UINT lightCommand(int light, bool zoomed) {
    switch (light) {
        case 0: return SC_CLOSE;
        case 1: return SC_MINIMIZE;
        default: return zoomed ? SC_RESTORE : SC_MAXIMIZE;
    }
}

std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples) {
    if (samples.empty()) return 0;
    struct Bucket { int n = 0; std::uint64_t r = 0, g = 0, b = 0; std::size_t first = 0; };
    std::map<std::uint32_t, Bucket> buckets;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::uint32_t c = samples[i];
        const std::uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        Bucket& k = buckets[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
        if (k.n == 0) k.first = i;
        ++k.n;
        k.r += r;
        k.g += g;
        k.b += b;
    }
    const Bucket* best = nullptr;
    for (const auto& [key, k] : buckets)
        if (!best || k.n > best->n || (k.n == best->n && k.first < best->first)) best = &k;
    return std::uint32_t(((best->r / best->n) << 16) | ((best->g / best->n) << 8) | (best->b / best->n));
}

namespace {

struct Rgb { double r, g, b; };
Rgb rgb(std::uint32_t c) { return {double((c >> 16) & 0xFF), double((c >> 8) & 0xFF), double(c & 0xFF)}; }

double segmentDistance(double px, double py, double ax, double ay, double bx, double by) {
    const double vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    const double len = vx * vx + vy * vy;
    const double t = len > 0 ? std::clamp((wx * vx + wy * vy) / len, 0.0, 1.0) : 0.0;
    const double dx = px - (ax + t * vx), dy = py - (ay + t * vy);
    return std::sqrt(dx * dx + dy * dy);
}

} // namespace

std::vector<std::uint8_t> renderLights(const LightsLayout& l, const LightsState& s, double scale) {
    const int w = int(l.window.right - l.window.left), h = int(l.window.bottom - l.window.top);
    std::vector<std::uint8_t> out(std::size_t(std::max(w, 0)) * std::max(h, 0) * 4, 0);
    if (w <= 0 || h <= 0) return out;
    static constexpr std::uint32_t kFill[3] = {0xFF5F57, 0xFEBC2E, 0x28C840}, kEdge[3] = {0xE0443E, 0xDEA123, 0x1AAB29};
    const std::uint32_t grayFill = s.dark ? 0x5A5A5A : 0xD0D0D0, grayEdge = s.dark ? 0x4A4A4A : 0xB8B8B8;
    const double border = std::max(0.5 * scale, 0.75), stroke = 1.1 * scale / 2, arm = 2.6 * scale;
    const double fade = kTail * scale;
    const Rgb patch = rgb(s.patchColor);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // Fond : couleur de la barre de titre, fondu sur la fin à droite.
            const double pa = std::clamp((w - (x + 0.5)) / fade, 0.0, 1.0);
            double a = pa, r = patch.r * pa, g = patch.g * pa, b = patch.b * pa;   // prémultiplié
            for (int i = 0; i < 3; ++i) {
                const double cx = (l.circles[i].left + l.circles[i].right) / 2.0 - l.window.left;
                const double cy = (l.circles[i].top + l.circles[i].bottom) / 2.0 - l.window.top;
                if (std::abs(x + 0.5 - cx) > l.radius + 1 || std::abs(y + 0.5 - cy) > l.radius + 1) continue;
                int outer = 0, inner = 0, glyph = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4;
                        const double d = std::hypot(px - cx, py - cy);
                        if (d > l.radius) continue;
                        ++outer;
                        if (d <= l.radius - border) ++inner;
                        if (!s.hover || !s.enabled[i]) continue;
                        double dist = 1e9;
                        if (i == 0) {   // ×
                            dist = std::min(segmentDistance(px, py, cx - arm, cy - arm, cx + arm, cy + arm),
                                            segmentDistance(px, py, cx - arm, cy + arm, cx + arm, cy - arm));
                        } else {        // − et +
                            dist = segmentDistance(px, py, cx - arm, cy, cx + arm, cy);
                            if (i == 2) dist = std::min(dist, segmentDistance(px, py, cx, cy - arm, cx, cy + arm));
                        }
                        if (dist <= stroke) ++glyph;
                    }
                if (!outer) continue;
                const Rgb fill = rgb(s.enabled[i] ? kFill[i] : grayFill), edge = rgb(s.enabled[i] ? kEdge[i] : grayEdge);
                const double cov = outer / 16.0, fin = inner / double(outer);
                Rgb c{edge.r + (fill.r - edge.r) * fin, edge.g + (fill.g - edge.g) * fin, edge.b + (fill.b - edge.b) * fin};
                const double ga = glyph / 16.0 * 0.55;   // symbole sombre à 55 %
                c = {c.r * (1 - ga), c.g * (1 - ga), c.b * (1 - ga)};
                r = c.r * cov + r * (1 - cov);
                g = c.g * cov + g * (1 - cov);
                b = c.b * cov + b * (1 - cov);
                a = cov + a * (1 - cov);
            }
            std::uint8_t* p = &out[(std::size_t(y) * w + x) * 4];
            p[0] = std::uint8_t(std::lround(std::clamp(b, 0.0, 255.0)));
            p[1] = std::uint8_t(std::lround(std::clamp(g, 0.0, 255.0)));
            p[2] = std::uint8_t(std::lround(std::clamp(r, 0.0, 255.0)));
            p[3] = std::uint8_t(std::lround(std::clamp(a * 255, 0.0, 255.0)));
        }
    return out;
}

std::vector<std::uint8_t> lightsSheet(UINT& w, UINT& h) {
    const LightsLayout l = lightsLayout(RECT{0, 0, 600, 400}, RECT{16, 62, 584, 384}, 192);
    const int cw = int(l.window.right - l.window.left), ch = int(l.window.bottom - l.window.top), pad = 24;
    w = UINT(3 * (cw + 2 * pad));
    h = UINT(2 * (ch + 2 * pad));
    std::vector<std::uint8_t> sheet(std::size_t(w) * h * 4, 0);
    for (int row = 0; row < 2; ++row) {
        const std::uint32_t bar = row ? 0x2B2B2B : 0xF3F3F3;
        for (int col = 0; col < 3; ++col) {
            LightsState st;
            st.dark = row == 1;
            st.patchColor = bar;
            st.hover = col == 1;
            if (col == 2) st.enabled[1] = st.enabled[2] = false;   // dialogue : fermer seulement
            const auto img = renderLights(l, st, 2.0);
            const int ox = col * (cw + 2 * pad), oy = row * (ch + 2 * pad);
            const Rgb back = rgb(bar);
            for (int y = 0; y < ch + 2 * pad; ++y)
                for (int x = 0; x < cw + 2 * pad; ++x) {
                    std::uint8_t* p = &sheet[(std::size_t(oy + y) * w + ox + x) * 4];
                    double r = back.r, g = back.g, b = back.b;   // la barre de titre autour
                    const int ix = x - pad, iy = y - pad;
                    if (ix >= 0 && iy >= 0 && ix < cw && iy < ch) {
                        const std::uint8_t* q = &img[(std::size_t(iy) * cw + ix) * 4];
                        const double ia = q[3] / 255.0;
                        r = q[2] + r * (1 - ia);
                        g = q[1] + g * (1 - ia);
                        b = q[0] + b * (1 - ia);
                    }
                    p[0] = std::uint8_t(std::lround(b));
                    p[1] = std::uint8_t(std::lround(g));
                    p[2] = std::uint8_t(std::lround(r));
                    p[3] = 255;
                }
        }
    }
    return sheet;
}

} // namespace md
