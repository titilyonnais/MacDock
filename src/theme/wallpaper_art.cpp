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
double smoothstep(double e0, double e1, double x) {
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

// Grille 3 × 3 de couleurs sur l'écran (u, v dans [0, 1]), interpolée en bilinéaire.
using Grid = Rgb[3][3];   // [ligne v][colonne u]
Rgb sample(const Grid& g, double u, double v) {
    u = std::clamp(u, 0.0, 1.0) * 2;
    v = std::clamp(v, 0.0, 1.0) * 2;
    const int i = std::min(1, int(u)), j = std::min(1, int(v));
    const double fu = u - i, fv = v - j;
    return mix(mix(g[j][i], g[j][i + 1], fu), mix(g[j + 1][i], g[j + 1][i + 1], fu), fv);
}

// Les quatre plis (bord droit de chaque feuille), relevés sur le fond Golden Gate : position horizontale u (en
// largeurs d'écran) tous les 0,04 de hauteur, de v = 0 à v = 1. Les feuilles descendent du haut, s'enroulent vers
// la droite puis partent en bas à gauche ; les deux dernières entrent par le bord droit (u > 1 : hors écran).
constexpr int kRows = 26;
constexpr double kCreases[4][kRows] = {
    {0.300, 0.314, 0.336, 0.349, 0.352, 0.351, 0.345, 0.331, 0.316, 0.291, 0.265, 0.234, 0.203,
     0.170, 0.139, 0.109, 0.083, 0.060, 0.041, 0.026, 0.014, 0.006, 0.000, -0.006, -0.012, -0.018},
    {0.670, 0.690, 0.704, 0.718, 0.723, 0.719, 0.706, 0.681, 0.646, 0.601, 0.545, 0.481, 0.415,
     0.349, 0.287, 0.231, 0.182, 0.140, 0.102, 0.072, 0.048, 0.029, 0.014, 0.003, -0.008, -0.018},
    {1.030, 1.010, 0.990, 0.976, 0.955, 0.926, 0.891, 0.850, 0.804, 0.755, 0.704, 0.652, 0.603,
     0.555, 0.511, 0.470, 0.432, 0.400, 0.371, 0.349, 0.330, 0.315, 0.305, 0.297, 0.294, 0.292},
    {1.080, 1.050, 1.020, 1.000, 0.983, 0.969, 0.951, 0.930, 0.906, 0.882, 0.859, 0.835, 0.811,
     0.789, 0.767, 0.748, 0.730, 0.715, 0.703, 0.693, 0.684, 0.677, 0.674, 0.670, 0.669, 0.668},
};

// Relevés lissés (trois passes de moyenne 1-2-1, extrémités gardées) : la mesure au pixel près fait des bosses.
const auto& smoothCreases() {
    static const auto table = [] {
        std::vector<std::vector<double>> out(4, std::vector<double>(kRows));
        for (int k = 0; k < 4; ++k) {
            std::vector<double> c(kCreases[k], kCreases[k] + kRows);
            for (int pass = 0; pass < 3; ++pass) {
                std::vector<double> n = c;
                for (int i = 1; i + 1 < kRows; ++i) n[std::size_t(i)] = (c[std::size_t(i - 1)] + 2 * c[std::size_t(i)] + c[std::size_t(i + 1)]) / 4;
                c = n;
            }
            out[std::size_t(k)] = c;
        }
        return out;
    }();
    return table;
}

// Position du pli k à la hauteur v (Catmull-Rom) et sa pente du/dv.
void crease(int k, double v, double& u, double& slope) {
    const double f = std::clamp(v, 0.0, 1.0) * (kRows - 1);
    const int i = std::min(kRows - 2, int(f));
    const double t = f - i;
    const double* c = smoothCreases()[std::size_t(k)].data();
    const double p0 = c[std::max(0, i - 1)], p1 = c[i], p2 = c[i + 1], p3 = c[std::min(kRows - 1, i + 2)];
    u = 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t + (-p0 + 3 * p1 - 3 * p2 + p3) * t * t * t);
    slope = 0.5 * ((-p0 + p2) + 2 * (2 * p0 - 5 * p1 + 4 * p2 - p3) * t + 3 * (-p0 + 3 * p1 - 3 * p2 + p3) * t * t) * (kRows - 1);
}

struct Palette {
    Grid shade, lit;     // côté ombre (juste après un pli) et côté éclairé (juste avant le pli suivant)
    Rgb creaseColor;     // liseré du pli
    double creaseAlpha;  // intensité du liseré
    double gamma;        // montée de l'ombre vers la lumière à travers une feuille
};

// Couleurs relevées sur les deux variantes (clair : bruns dorés vers bleu-gris ; sombre : indigo presque noir aux
// plis lavande), lissées en grilles 3 × 3.
const Palette kLight = {
    {{hex(0x7B583D), hex(0x5E4C42), hex(0x504441)},
     {hex(0x9A7650), hex(0x6F5A52), hex(0x857B76)},
     {hex(0x4C3F38), hex(0x3C3749), hex(0x4A5768)}},
    {{hex(0xE2D5C5), hex(0xEFE8DF), hex(0xD2CAC2)},
     {hex(0xF6EEDF), hex(0xE4D7C7), hex(0xE2E1DB)},
     {hex(0x8E8085), hex(0x6F7E94), hex(0x8A9096)}},
    hex(0xFFFAF0), 0.85, 1.25,
};
const Palette kDark = {
    {{hex(0x1C1B2B), hex(0x171521), hex(0x13141F)},
     {hex(0x2E2B42), hex(0x11121B), hex(0x0F1125)},
     {hex(0x0C0A12), hex(0x0E0C16), hex(0x191B2A)}},
    {{hex(0x323153), hex(0x464875), hex(0x313350)},
     {hex(0x414063), hex(0x44496B), hex(0x61749B)},
     {hex(0x2C2D41), hex(0x2A2B42), hex(0x4F5B80)}},
    hex(0x9EA0D6), 0.9, 2.0,
};

} // namespace

// Fond Golden Gate dessiné par le code (aucun fichier Apple) : cinq grandes feuilles en S qui se recouvrent. Chacune
// part de l'ombre portée par la feuille de gauche et s'éclaircit jusqu'à son pli, bordé d'un liseré net. Calculé par
// bandes de lignes sur tous les cœurs.
BgraImage macWallpaper(int w, int h, bool dark) {
    BgraImage im{w, h, std::vector<std::uint8_t>(std::size_t(std::max(w, 0)) * std::max(h, 0) * 4, 0)};
    if (w <= 0 || h <= 0) return im;
    const Palette& pal = dark ? kDark : kLight;
    const double aspect = double(w) / h;
    const double lineW = std::max(0.9, w / 2600.0);   // liseré : ~1,5 px en 4K
    auto rows = [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const double v = (y + 0.5) / h;
            double cu[4], perp[4];
            for (int k = 0; k < 4; ++k) {
                double slope = 0;
                crease(k, v, cu[k], slope);
                perp[k] = 1 / std::sqrt(1 + slope * slope * aspect * aspect);   // horizontal → distance au pli
            }
            // Couleur de la feuille s (0 à 4) en u : de l'ombre (après le pli de gauche) à la lumière (avant le sien).
            auto sheet = [&](int s, double u) {
                const double left = s == 0 ? cu[0] - 0.42 : cu[s - 1], right = s == 4 ? 1.10 : cu[s];
                const double t = std::clamp((u - left) / std::max(right - left, 1e-3), 0.0, 1.0);
                return mix(sample(pal.shade, u, v), sample(pal.lit, u, v), std::pow(t, pal.gamma));
            };
            for (int x = 0; x < w; ++x) {
                const double u = (x + 0.5) / w;
                int s = 0;
                while (s < 4 && u >= cu[s]) ++s;
                Rgb c = sheet(s, u);
                // Pli le plus proche (à gauche : on sort de son ombre ; à droite : on arrive sur sa crête).
                for (int k = std::max(0, s - 1); k <= std::min(3, s); ++k) {
                    const double dpx = (u - cu[k]) * w * perp[k];   // < 0 : sur la feuille du pli, > 0 : dessous
                    if (std::fabs(dpx) < 1) c = mix(sheet(k, u), sheet(k + 1, u), smoothstep(-1, 1, dpx));   // anticrénelage
                    if (dpx < 0 && dpx > -6 * lineW) {
                        const double a = pal.creaseAlpha * std::exp(-(dpx / lineW) * (dpx / lineW)) +
                                         0.18 * std::exp(dpx / (3 * lineW));   // liseré net + léger reflet
                        c = mix(c, pal.creaseColor, std::min(1.0, a));
                    }
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
