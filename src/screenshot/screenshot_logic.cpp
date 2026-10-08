#include "screenshot_logic.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace md {

namespace {

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }

// Flou en boîte, trois passes par axe (proche d'un flou gaussien d'écart sigma), sur un plan de w × h valeurs.
void blurPlane(std::vector<float>& v, int w, int h, double sigma) {
    if (sigma < 0.5 || w <= 0 || h <= 0) return;
    const int r = std::max(1, int(std::lround(std::sqrt(4.0 * sigma * sigma + 1) / 2)));   // trois boîtes de 2r+1
    std::vector<float> line(std::size_t(std::max(w, h)));
    const float inv = 1.0f / float(2 * r + 1);
    auto pass = [&](int count, int length, auto at) {
        for (int i = 0; i < count; ++i) {
            for (int k = 0; k < length; ++k) line[std::size_t(k)] = at(i, k);
            float sum = 0;
            for (int k = -r; k <= r; ++k) sum += k >= 0 && k < length ? line[std::size_t(k)] : 0;
            for (int k = 0; k < length; ++k) {
                at(i, k) = sum * inv;
                const int add = k + r + 1, drop = k - r;
                if (add < length) sum += line[std::size_t(add)];
                if (drop >= 0) sum -= line[std::size_t(drop)];
            }
        }
    };
    for (int n = 0; n < 3; ++n) {
        pass(h, w, [&](int y, int x) -> float& { return v[std::size_t(y) * w + x]; });
        pass(w, h, [&](int x, int y) -> float& { return v[std::size_t(y) * w + x]; });
    }
}

// Part d'un pixel (centre à x+0,5) couverte par un disque de centre (cx, cy) et de rayon r.
double discCoverage(double x, double y, double cx, double cy, double r) {
    const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
    return clamp01(r - d + 0.5);
}

} // namespace

std::wstring screenshotBaseName(const SYSTEMTIME& t) {
    wchar_t buf[96];
    swprintf_s(buf, L"Capture d’écran %04u-%02u-%02u à %02u.%02u.%02u", t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond);
    return buf;
}

std::wstring screenshotFileName(const std::wstring& base, int n) {
    return n <= 1 ? base + L".png" : base + L" (" + std::to_wstring(n) + L").png";
}

std::wstring uniqueScreenshotPath(const std::wstring& dir, const std::wstring& base, int first,
                                  const std::function<bool(const std::wstring&)>& exists) {
    std::wstring folder = dir;
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/')) folder.pop_back();
    int n = std::max(first, 1);
    std::wstring path;
    for (int tries = 0; tries < 1000; ++tries, ++n) {
        path = folder + L"\\" + screenshotFileName(base, n);
        if (!exists || !exists(path)) break;
    }
    return path;
}

ShotKey screenshotKey(const ShotKeyEvent& e) {
    if (e.vk != '3' && e.vk != '4') return ShotKey::Pass;
    if (!e.down || e.repeat) return e.taken ? ShotKey::Swallow : ShotKey::Pass;
    if (e.injected || !e.mods.win || !e.mods.shift || e.mods.alt) return ShotKey::Pass;
    return e.vk == '3' ? ShotKey::Screen : ShotKey::Region;
}

ShotSessionKey screenshotSessionKey(unsigned vk, bool down, bool repeat) {
    if (vk == VK_ESCAPE) return down && !repeat ? ShotSessionKey::Cancel : ShotSessionKey::Swallow;
    if (vk == VK_SPACE) return down && !repeat ? ShotSessionKey::ToggleWindow : ShotSessionKey::Swallow;
    return ShotSessionKey::Pass;
}

RECT selectionRect(POINT a, POINT b, const RECT& bounds) {
    auto cx = [&](LONG v) { return std::clamp(v, bounds.left, bounds.right); };
    auto cy = [&](LONG v) { return std::clamp(v, bounds.top, bounds.bottom); };
    return RECT{cx(std::min(a.x, b.x)), cy(std::min(a.y, b.y)), cx(std::max(a.x, b.x)), cy(std::max(a.y, b.y))};
}

bool selectionUsable(const RECT& r) { return r.right - r.left >= 4 && r.bottom - r.top >= 4; }

RECT thumbnailRect(const RECT& work, SIZE image, double scale) {
    const double w = std::max<LONG>(image.cx, 1), h = std::max<LONG>(image.cy, 1);
    const double fit = std::min({1.0, 200 * scale / w, 150 * scale / h});
    const LONG tw = std::max(1L, LONG(std::lround(w * fit))), th = std::max(1L, LONG(std::lround(h * fit)));
    const LONG margin = LONG(std::lround(20 * scale));
    const LONG right = work.right - margin, bottom = work.bottom - margin;
    return RECT{right - tw, bottom - th, right, bottom};
}

double thumbnailOffset(ThumbPhase phase, double t, double distance) {
    if (phase == ThumbPhase::In) {
        const double u = clamp01(t / kThumbIn);
        return distance * std::pow(1 - u, 3);   // 1 - easeOutCubic
    }
    const double u = clamp01(t / kThumbOut);
    return distance * u * u;
}

void roundCorners(BgraImage& img, double radius) {
    const double r = std::min({radius, img.w / 2.0, img.h / 2.0});
    if (r <= 0 || img.px.size() < std::size_t(img.w) * img.h * 4) return;
    const int span = int(std::ceil(r));
    for (int y = 0; y < img.h; ++y) {
        const bool top = y < span, bottom = y >= img.h - span;
        if (!top && !bottom) continue;
        for (int x = 0; x < img.w; ++x) {
            const bool left = x < span, right = x >= img.w - span;
            if (!left && !right) continue;
            const double cx = left ? r : img.w - r, cy = top ? r : img.h - r;
            const bool outside = (left ? x + 0.5 < cx : x + 0.5 > cx) && (top ? y + 0.5 < cy : y + 0.5 > cy);
            if (!outside) continue;   // bande droite du bord : pleine
            std::uint8_t& a = img.px[(std::size_t(y) * img.w + x) * 4 + 3];
            a = std::uint8_t(std::lround(a * discCoverage(x, y, cx, cy, r)));
        }
    }
}

ShadowSpec windowShadowSpec(double scale) {
    ShadowSpec s;
    s.left = s.right = int(std::lround(28 * scale));
    s.top = int(std::lround(18 * scale));
    s.bottom = int(std::lround(38 * scale));
    s.blur = 12 * scale;
    s.opacity = 0.45;
    s.offsetY = int(std::lround(10 * scale));
    return s;
}

BgraImage withShadow(const BgraImage& img, const ShadowSpec& spec) {
    BgraImage out;
    if (img.w <= 0 || img.h <= 0 || img.px.size() < std::size_t(img.w) * img.h * 4) return out;
    out.w = img.w + spec.left + spec.right;
    out.h = img.h + spec.top + spec.bottom;
    // Masque de l'ombre : alpha de l'image, décalé vers le bas, puis flou.
    std::vector<float> mask(std::size_t(out.w) * out.h, 0.0f);
    for (int y = 0; y < img.h; ++y) {
        const int oy = y + spec.top + spec.offsetY;
        if (oy < 0 || oy >= out.h) continue;
        for (int x = 0; x < img.w; ++x)
            mask[std::size_t(oy) * out.w + x + spec.left] = img.px[(std::size_t(y) * img.w + x) * 4 + 3] / 255.0f;
    }
    blurPlane(mask, out.w, out.h, spec.blur);
    out.px.assign(std::size_t(out.w) * out.h * 4, 0);
    for (std::size_t i = 0; i < mask.size(); ++i)
        out.px[i * 4 + 3] = std::uint8_t(std::lround(clamp01(mask[i] * spec.opacity) * 255));   // ombre noire
    // L'image par-dessus (source sur destination, alpha non prémultiplié).
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            const std::uint8_t* s = &img.px[(std::size_t(y) * img.w + x) * 4];
            std::uint8_t* d = &out.px[(std::size_t(y + spec.top) * out.w + x + spec.left) * 4];
            const double as = s[3] / 255.0, ad = d[3] / 255.0;
            const double ao = as + ad * (1 - as);
            if (ao <= 0) continue;
            for (int c = 0; c < 3; ++c)   // l'ombre est noire : seule la couleur de l'image compte
                d[c] = std::uint8_t(std::lround(std::clamp(s[c] * as / ao, 0.0, 255.0)));
            d[3] = std::uint8_t(std::lround(ao * 255));
        }
    return out;
}

std::vector<std::uint8_t> premultiply(const BgraImage& img) {
    std::vector<std::uint8_t> out(img.px.size());
    for (std::size_t i = 0; i + 3 < img.px.size(); i += 4) {
        const unsigned a = img.px[i + 3];
        for (int c = 0; c < 3; ++c) out[i + c] = std::uint8_t((img.px[i + c] * a + 127) / 255);
        out[i + 3] = std::uint8_t(a);
    }
    return out;
}

std::vector<std::uint8_t> thumbnailPixels(const BgraImage& reduced, double scale, int& w, int& h, int& margin) {
    w = h = margin = 0;
    if (reduced.w <= 0 || reduced.h <= 0 || reduced.px.size() < std::size_t(reduced.w) * reduced.h * 4) return {};
    BgraImage img = reduced;
    // Liseré sombre très fin le long du bord (la vignette se détache aussi sur un fond clair).
    const int edge = std::max(1, int(std::lround(scale)));
    for (int y = 0; y < img.h; ++y)
        for (int x = 0; x < img.w; ++x) {
            if (x >= edge && y >= edge && x < img.w - edge && y < img.h - edge) continue;
            std::uint8_t* p = &img.px[(std::size_t(y) * img.w + x) * 4];
            for (int c = 0; c < 3; ++c) p[c] = std::uint8_t(p[c] * 0.78);
        }
    roundCorners(img, 5 * scale);
    ShadowSpec spec;
    margin = int(std::lround(12 * scale));
    spec.left = spec.top = spec.right = spec.bottom = margin;
    spec.blur = 4 * scale;
    spec.opacity = 0.35;
    spec.offsetY = int(std::lround(2 * scale));
    const BgraImage out = withShadow(img, spec);
    w = out.w;
    h = out.h;
    return premultiply(out);
}

bool rectOnScreens(const RECT& r, const std::vector<RECT>& screens) {
    const long long want = (long long)(r.right - r.left) * (r.bottom - r.top);
    if (want <= 0) return false;
    long long covered = 0;   // les écrans ne se chevauchent pas : les parts s'additionnent
    for (const RECT& s : screens) {
        const LONG l = std::max(r.left, s.left), t = std::max(r.top, s.top);
        const LONG rr = std::min(r.right, s.right), b = std::min(r.bottom, s.bottom);
        if (rr > l && b > t) covered += (long long)(rr - l) * (b - t);
    }
    return covered >= want;
}

BgraImage flattenOn(const BgraImage& img, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    BgraImage out = img;
    const std::uint8_t bg[3] = {b, g, r};
    for (std::size_t i = 0; i + 3 < out.px.size(); i += 4) {
        const unsigned a = out.px[i + 3];
        for (int c = 0; c < 3; ++c) out.px[i + c] = std::uint8_t((out.px[i + c] * a + bg[c] * (255 - a) + 127) / 255);
        out.px[i + 3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> cameraCursorPixels(int size) {
    if (size <= 0) return {};
    const double s = size;
    // Formes en unités de la taille : corps arrondi, bosse du viseur, objectif.
    auto inBody = [&](double x, double y, double grow) {
        const double l = 0.10 * s - grow, t = 0.34 * s - grow, r = 0.90 * s + grow, b = 0.84 * s + grow;
        const double rad = 0.10 * s + grow;
        if (x < l || x > r || y < t || y > b) {
            // bosse au-dessus du corps
            return x >= 0.34 * s - grow && x <= 0.66 * s + grow && y >= 0.22 * s - grow && y <= 0.36 * s;
        }
        const double cx = std::clamp(x, l + rad, r - rad), cy = std::clamp(y, t + rad, b - rad);
        return std::hypot(x - cx, y - cy) <= rad;
    };
    const double lx = 0.5 * s, ly = 0.59 * s;
    std::vector<std::uint8_t> px(std::size_t(size) * size * 4, 0);
    constexpr int kSub = 4;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            double white = 0, black = 0;
            for (int sy = 0; sy < kSub; ++sy)
                for (int sx = 0; sx < kSub; ++sx) {
                    const double fx = x + (sx + 0.5) / kSub, fy = y + (sy + 0.5) / kSub;
                    const double d = std::hypot(fx - lx, fy - ly);
                    if (inBody(fx, fy, 0)) {
                        if (d <= 0.10 * s || d > 0.17 * s) black += 1;   // corps et centre de l'objectif
                        else white += 1;                                  // bague de l'objectif
                    } else if (inBody(fx, fy, 0.06 * s)) {
                        white += 1;   // contour
                    }
                }
            const double n = kSub * kSub, a = (white + black) / n;
            if (a <= 0) continue;
            const std::uint8_t v = std::uint8_t(std::lround(255 * white / (white + black)));
            std::uint8_t* p = &px[(std::size_t(y) * size + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = std::uint8_t(std::lround(a * 255));
        }
    return px;
}

} // namespace md
