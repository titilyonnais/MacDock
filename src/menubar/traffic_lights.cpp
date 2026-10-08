#include "traffic_lights.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace md {

namespace {
constexpr const wchar_t* kShellClasses[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW"};
// Golden Gate (comme Tahoe) : pastilles de 14 pt, 23 pt de centre à centre (mesuré par des développeurs).
constexpr double kDiameter = 14, kSpacing = 23, kFirst = 20, kTail = 8, kMinTitle = 20, kDefaultTitle = 28;   // points
constexpr double kTopGap = 3;   // points laissés au bord du haut (redimensionnement)

bool isButtonHit(LRESULT h) { return h == HTMINBUTTON || h == HTMAXBUTTON || h == HTCLOSE; }
}

bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT) {
    if (mode == LightsMode::Off || w.iconic || w.ownProcess || w.elevated) return false;
    // Menu système, ou au moins un bouton réduire ou agrandir (Electron sans cadre n'a pas de menu système).
    if ((w.style & WS_CAPTION) != WS_CAPTION || (w.style & WS_CHILD)) return false;
    if (!(w.style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))) return false;
    if (w.exStyle & WS_EX_TOOLWINDOW) return false;
    for (const wchar_t* c : kShellClasses)
        if (w.className == c) return false;
    return true;
}

RECT captionButtons(const RECT& window, const RECT& frame, const RECT& dwm, UINT dpi, const HitProbe& hit) {
    if (dwm.right > dwm.left && dwm.bottom > dwm.top) {   // rognées au cadre visible (bordures invisibles exclues)
        const RECT r{std::max(frame.left, window.left + dwm.left), std::max(frame.top, window.top + dwm.top),
                     std::min(frame.right, window.left + dwm.right), std::min(frame.bottom, window.top + dwm.bottom)};
        return r.right > r.left && r.bottom > r.top ? r : RECT{};
    }
    if (!hit) return {};
    const double k = (dpi ? dpi : 96) / 96.0;
    const LONG step = std::max(1L, LONG(std::lround(2 * k)));
    const LONG y = frame.top + std::lround(12 * k);   // à mi-hauteur des boutons dessinés par l'app (~30 pt)
    LONG left = -1, right = -1;
    int misses = 0;
    for (LONG x = frame.right - 1; x > frame.left && x > frame.right - std::lround(260 * k); x -= step) {
        if (isButtonHit(hit(POINT{x, y}))) {
            if (right < 0) right = x + 1;
            left = x;
            misses = 0;
        } else if (right >= 0 && ++misses >= 3) {
            break;
        }
    }
    if (right < 0) return {};
    while (left - 1 > frame.left && left - 1 > left - step && isButtonHit(hit(POINT{left - 1, y}))) --left;
    if (right >= frame.right - step) right = frame.right;   // le dernier bouton touche le bord
    const LONG x = (left + right) / 2;
    LONG bottom = y + 1;
    for (LONG yy = y; yy < frame.top + std::lround(80 * k) && isButtonHit(hit(POINT{x, yy})); yy += step) bottom = yy + 1;
    while (bottom < frame.bottom && isButtonHit(hit(POINT{x, bottom}))) {
        ++bottom;
        if (bottom > frame.top + std::lround(80 * k)) break;
    }
    return RECT{left, frame.top, right, bottom};
}

bool leftCaptionFree(const RECT& frame, LONG titleBottom, UINT dpi, const HitProbe& hit) {
    if (!hit) return false;
    const double k = (dpi ? dpi : 96) / 96.0;
    LONG titleH = titleBottom - frame.top;
    if (titleH < std::lround(kMinTitle * k)) titleH = std::lround(kDefaultTitle * k);
    const LONG cy = frame.top + titleH / 2, r = std::lround(kDiameter / 2 * k);
    const LONG step = std::max(2L, LONG(std::lround(4 * k)));
    const LONG end = frame.left + std::lround((kFirst + 2 * kSpacing + kDiameter / 2 + kTail) * k);
    for (LONG y : {cy - r, cy, cy + r})
        for (LONG x = frame.left + std::lround(4 * k); x < end; x += step) {
            const LRESULT h = hit(POINT{x, y});
            if (h != HTCAPTION && h != HTSYSMENU) return false;
        }
    return true;
}

LightsLayout lightsOverButtons(const RECT& buttons, UINT dpi, bool zoomed) {
    const double k = (dpi ? dpi : 96) / 96.0;
    LightsLayout l;
    l.window = buttons;
    const LONG need = std::lround((2 * kSpacing + kDiameter + 2 * kTail) * k);
    if (l.window.right - l.window.left < need) l.window.left = l.window.right - need;   // un seul bouton (dialogue)
    const LONG r = std::lround(kDiameter / 2 * k);
    l.radius = double(r);
    const LONG cy = (buttons.top + buttons.bottom) / 2, mid = (l.window.left + l.window.right) / 2;
    for (int i = 0; i < 3; ++i) {
        const LONG cx = mid + std::lround((i - 1) * kSpacing * k);
        l.circles[i] = RECT{cx - r, cy - r, cx + r, cy + r};
    }
    l.fade = false;
    l.topGap = zoomed ? 0 : std::min(LONG(std::lround(kTopGap * k)), (buttons.bottom - buttons.top) / 4);
    l.patch = l.window;
    l.patch.top += l.topGap;
    return l;
}

LightsLayout buttonsCover(const RECT& buttons, UINT dpi, bool zoomed) {
    LightsLayout l = lightsOverButtons(buttons, dpi, zoomed);
    l.window = buttons;
    l.patch = buttons;
    l.patch.top += l.topGap;
    l.lights = false;
    return l;
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

LightsLayout lightsLayoutFor(const LightsWindowInfo& w, UINT dpi) {
    RECT title = w.client;
    if (w.captionBottom > w.frame.top && w.captionBottom < w.client.top) title.top = w.captionBottom;
    return lightsLayout(w.frame, title, dpi);
}

LightsMouse lightsMouse(bool doubleClick, int hit, const bool enabled[3]) {
    if (hit < 0) return doubleClick ? LightsMouse::Zoom : LightsMouse::Drag;
    return !doubleClick && enabled[hit] ? LightsMouse::Press : LightsMouse::None;
}

UINT captionDoubleClick(bool maximizable, bool zoomed) {
    if (!maximizable) return 0;
    return zoomed ? SC_RESTORE : SC_MAXIMIZE;
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
    // Golden Gate : verre façon Aqua, teintes d'avant 27 désaturées de ~12 %, liseré plus sombre, reflet elliptique
    // en haut et lueur plus faible en bas (pas de valeurs publiées par Apple : estimations du rapport de recherche).
    static constexpr std::uint32_t kFill[3] = {0xE26E65, 0xF0BE5E, 0x68C05D}, kEdge[3] = {0xC4483F, 0xD29C38, 0x3E9C3A};
    const std::uint32_t grayFill = s.dark ? 0x4E4F52 : 0xDDDDDD, grayEdge = s.dark ? 0x3E3F42 : 0xC4C3C6;
    const double border = std::max(0.5 * scale, 0.75), stroke = 1.1 * scale / 2, arm = 2.9 * scale;
    const double fade = kTail * scale;
    const Rgb patch = rgb(s.patchColor);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            // Fond : couleur de la barre de titre (fondu sur la fin à droite pour des pastilles posées à gauche) ; le
            // bord du haut reste transparent quand il faut pouvoir y redimensionner la fenêtre.
            double pa = l.fade ? std::clamp((w - (x + 0.5)) / fade, 0.0, 1.0) : 1.0;
            if (y < l.topGap) pa = 0;
            double a = pa, r = patch.r * pa, g = patch.g * pa, b = patch.b * pa;   // prémultiplié
            for (int i = 0; i < 3 && l.lights; ++i) {
                const double cx = (l.circles[i].left + l.circles[i].right) / 2.0 - l.window.left;
                const double cy = (l.circles[i].top + l.circles[i].bottom) / 2.0 - l.window.top;
                if (std::abs(x + 0.5 - cx) > l.radius + 1 || std::abs(y + 0.5 - cy) > l.radius + 1) continue;
                const bool down = s.pressed == i && s.enabled[i];
                const double radius = l.radius * (down ? 0.94 : i == s.bouncing ? s.bounce : 1.0);   // enfoncée sous le doigt
                const Rgb fill = rgb(s.enabled[i] ? kFill[i] : grayFill), edge = rgb(s.enabled[i] ? kEdge[i] : grayEdge);
                int outer = 0, glyph = 0;
                double sr = 0, sg = 0, sb = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) {
                        const double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4;
                        const double d = std::hypot(px - cx, py - cy);
                        if (d > radius) continue;
                        ++outer;
                        // Verre : liseré plus sombre, teinte un peu plus claire en haut, reflet en haut, lueur en bas.
                        const double u = (px - cx) / radius, v = (py - cy) / radius;
                        Rgb c = d > radius - border ? edge : fill;
                        const double shade = (1.0 - 0.07 * v) * (down ? 0.78 : 1.0);
                        c = {c.r * shade, c.g * shade, c.b * shade};
                        if (s.enabled[i]) {
                            const double hx = u / 0.62, hy = (v + 0.52) / 0.34, gx = u / 0.55, gy = (v - 0.64) / 0.22;
                            double white = 0;
                            if (hx * hx + hy * hy < 1) white += (down ? 0.25 : 0.5) * (1 - (hx * hx + hy * hy));
                            if (gx * gx + gy * gy < 1) white += (down ? 0.08 : 0.2) * (1 - (gx * gx + gy * gy));
                            c = {c.r + (255 - c.r) * white, c.g + (255 - c.g) * white, c.b + (255 - c.b) * white};
                        }
                        sr += c.r;
                        sg += c.g;
                        sb += c.b;
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
                const double cov = outer / 16.0;
                Rgb c{sr / outer, sg / outer, sb / outer};
                const double ga = glyph / 16.0 * (down ? 0.7 : 0.55);   // symbole sombre
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
