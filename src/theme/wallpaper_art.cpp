#include "wallpaper_art.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace md {

namespace {

struct Rgb { double r, g, b; };
constexpr Rgb hex(std::uint32_t c) { return {double((c >> 16) & 0xFF), double((c >> 8) & 0xFF), double(c & 0xFF)}; }
Rgb mix(Rgb a, Rgb b, double t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
Rgb scale(Rgb a, double k) { return {a.r * k, a.g * k, a.b * k}; }
double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Dégradé du fond à quatre arrêts (0, 1/3, 2/3, 1).
Rgb ramp(const Rgb (&stops)[4], double t) {
    t = std::clamp(t, 0.0, 1.0) * 3;
    const int i = std::min(2, int(t));
    return mix(stops[i], stops[i + 1], smoothstep(0, 1, t - i));
}

// Un pli : grand disque légèrement ondulé centré hors de l'écran, en bas à gauche. Empilés du plus grand (derrière)
// au plus petit (devant), centres décalés tour à tour : chacun ne laisse voir du précédent qu'un croissant, épais
// d'un côté et effilé de l'autre, comme des pétales ou du papier plié.
struct Fold {
    double cx, cy, radius;   // en hauteurs d'écran
    double wobble, phase;    // ondulation du bord : radius × (1 + wobble × sin(3θ + phase))
    std::uint32_t light, dark;
};

constexpr Fold kFolds[] = {
    {-0.20, 1.55, 2.05, 0.035, 0.4, 0xA79BD0, 0x1E2160},   // lavande
    {-0.42, 1.30, 1.78, 0.045, 2.2, 0xB7B4CC, 0x2B2F7A},   // gris lavande / indigo
    {-0.18, 1.42, 1.50, 0.040, 3.4, 0xC9CCD6, 0x3B4166},   // argent / ardoise
    {-0.36, 1.22, 1.22, 0.050, 4.7, 0xDCCFBF, 0x4A4687},   // beige gris / indigo lavande
    {-0.14, 1.20, 0.98, 0.045, 5.9, 0xD9B97A, 0x6F63B5},   // or / lavande
    {-0.30, 1.12, 0.74, 0.050, 1.3, 0xF0DDB5, 0x8F7B5C},   // champagne / or éteint
};

// Bord d'un pli vu d'un pixel : distance signée (en hauteurs d'écran, négative dedans), éclairage par le haut
// gauche (-1 à 1, d'après la normale) et pente verticale de la distance (pour l'ombre portée, décalée vers le bas).
struct Edge { double d, light, slope; };

Edge foldEdge(const Fold& f, double cosPhase, double sinPhase, double u, double v) {
    const double dx = u - f.cx, dy = (v - f.cy) * 1.08;   // un peu aplati : plis de papier plutôt que cercles
    const double r = std::sqrt(dx * dx + dy * dy);
    if (r < 1e-9) return {-f.radius, 0, 0};
    const double s = dy / r, c = dx / r;   // sin(3θ + φ) sans trigonométrie inverse
    const double s3 = 3 * s - 4 * s * s * s, c3 = 4 * c * c * c - 3 * c;
    return {r - f.radius * (1 + f.wobble * (s3 * cosPhase + c3 * sinPhase)), -0.45 * c - 0.89 * s, 1.08 * s};
}

} // namespace

// Fond Golden Gate, recette originale (aucun fichier Apple) : grands plis courbes qui se chevauchent, de l'or sablé
// et du champagne en bas à gauche vers le gris, l'indigo et la lavande en haut à droite ; ombre douce de chaque pli
// sur celui de derrière, liseré clair (bleu argenté en sombre) sur le bord, tramage contre les bandes. Calculé par
// bandes de lignes sur tous les cœurs (≈ 0,1 s en 4K).
BgraImage macWallpaper(int w, int h, bool dark) {
    BgraImage im{w, h, std::vector<std::uint8_t>(std::size_t(std::max(w, 0)) * std::max(h, 0) * 4, 0)};
    if (w <= 0 || h <= 0) return im;
    static constexpr Rgb kLight[4] = {hex(0xEAD9B8), hex(0xD9CFC4), hex(0xB7B4CC), hex(0x8E8BB8)};
    static constexpr Rgb kDark[4] = {hex(0x2A2238), hex(0x1B1D3F), hex(0x11132E), hex(0x070818)};
    constexpr int kCount = int(std::size(kFolds));
    const auto& stops = dark ? kDark : kLight;
    const double aspect = double(w) / h, px = 1.0 / h;
    const double angle = 20 * 3.14159265358979 / 180, ax = std::cos(angle), ay = std::sin(angle);
    const double shadowOpacity = dark ? 0.45 : 0.18, blur = 0.035, drop = 0.0055;
    const double rimWidth = std::max(1.5 * px, 0.0014);
    const Rgb rimColor = dark ? hex(0xA9B8E0) : Rgb{255, 255, 255};
    const double rimOpacity = dark ? 0.40 : 0.35;
    double cosPhase[kCount], sinPhase[kCount];
    Rgb tint[kCount];
    for (int k = 0; k < kCount; ++k) {
        cosPhase[k] = std::cos(kFolds[k].phase);
        sinPhase[k] = std::sin(kFolds[k].phase);
        tint[k] = hex(dark ? kFolds[k].dark : kFolds[k].light);
    }
    auto rows = [&](int y0, int y1) {
        Edge e[kCount];
        for (int y = y0; y < y1; ++y) {
            const double v = (y + 0.5) / h;
            for (int x = 0; x < w; ++x) {
                const double u = (x + 0.5) / h;
                // Fond : dégradé à 20°, de gauche vers la droite et un peu vers le bas.
                const Rgb back = ramp(stops, (u * ax + v * ay) / (aspect * ax + ay));
                int first = 0;   // plis entièrement recouverts par un pli de devant : inutile de les peindre
                for (int k = 0; k < kCount; ++k) {
                    e[k] = foldEdge(kFolds[k], cosPhase[k], sinPhase[k], u, v);
                    if (e[k].d < -px - drop - blur * 2) first = k;
                }
                Rgb c = back;
                for (int k = first; k < kCount; ++k) {
                    const Edge& g = e[k];
                    // Ombre portée du pli sur ce qu'il recouvre (décalée vers le bas, floue).
                    const double ds = g.d - drop * g.slope;
                    if (ds > -blur && ds < blur * 2) c = scale(c, 1 - shadowOpacity * (1 - smoothstep(-blur * 0.4, blur * 1.6, ds)));
                    const double cover = 1 - smoothstep(-px, px, g.d);
                    if (cover <= 0) continue;
                    // Teinte du pli, mêlée au fond pour suivre le dégradé ; plus claire là où le bord fait face à la
                    // lumière (haut gauche), un peu plus sombre en s'éloignant du bord (de 100 % à ~90 %).
                    Rgb fill = scale(mix(tint[k], back, 0.22), (0.9 + 0.1 * smoothstep(-0.22, 0, g.d)) * (0.97 + 0.07 * g.light));
                    const double rd = (g.d + rimWidth) / rimWidth;
                    if (rd > -4 && rd < 4) fill = mix(fill, rimColor, rimOpacity * std::exp(-rd * rd));
                    c = mix(c, fill, cover);
                }
                std::uint32_t hsh = std::uint32_t(x) * 0x9E3779B1u ^ std::uint32_t(y) * 0x85EBCA77u;   // tramage déterministe
                hsh ^= hsh >> 15;
                hsh *= 0x2C1B3C6Du;
                hsh ^= hsh >> 12;
                const double dither = (hsh & 0xFF) / 255.0 - 0.5;
                std::uint8_t* p = &im.px[(std::size_t(y) * w + x) * 4];
                p[0] = std::uint8_t(std::clamp(std::lround(c.b + dither), 0L, 255L));
                p[1] = std::uint8_t(std::clamp(std::lround(c.g + dither), 0L, 255L));
                p[2] = std::uint8_t(std::clamp(std::lround(c.r + dither), 0L, 255L));
                p[3] = 255;
            }
        }
    };
    const int threads = std::clamp(int(std::thread::hardware_concurrency()), 1, 16);
    if (threads == 1 || std::size_t(w) * h < 200000) {
        rows(0, h);
        return im;
    }
    std::vector<std::thread> pool;
    const int band = (h + threads - 1) / threads;
    int done = 0;   // lignes confiées à un fil
    try {
        for (int t = 0; t < threads && t * band < h; ++t) {
            pool.emplace_back(rows, t * band, std::min(h, (t + 1) * band));
            done = std::min(h, (t + 1) * band);
        }
    } catch (...) {   // fil impossible à créer : le reste est calculé ici
    }
    if (done < h) rows(done, h);
    for (auto& t : pool) t.join();
    return im;
}

} // namespace md
