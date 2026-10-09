// Forme des icônes macOS (carré à coins continus de la grille Apple) et test de conformité (pur).
#pragma once
#include <cstdint>

namespace md {

constexpr double kIconShapeRatio = 824.0 / 1024.0;   // forme visible / case (grille Apple)
// Rayon / forme visible : 0,259 pour macOS 26 Tahoe, mesuré sur son Dock (plan 60) ; Big Sur à Sequoia : 185,4/824.
constexpr double kIconCornerRatio = 0.259;

// Point (x, y) en pixels dans un carré de côté size : à l'intérieur de la forme ?
bool insideSquircle(double x, double y, double size, double cornerRatio = kIconCornerRatio);

// Couverture anticrénelée (4x4) du pixel (px, py), lue dans un masque mis en cache par taille.
double squircleMaskAlpha(int px, int py, int size, double cornerRatio = kIconCornerRatio);

// Part des pixels de la forme qui sont opaques (alpha > 200) dans une image BGRA.
double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride);

// L'icône remplit-elle déjà la forme (sinon : « icon jail » de Tahoe) ?
bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride);

} // namespace md
