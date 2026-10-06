// Forme « squircle » des icônes macOS (superellipse) et test de conformité (pur).
#pragma once
#include <cstdint>

namespace md {

constexpr double kSquircleExponent = 5.0;

// Point (x, y) en pixels dans un carré de côté size : à l'intérieur de la superellipse ?
bool insideSquircle(double x, double y, double size, double exponent = kSquircleExponent);

// Couverture anticrénelée (suréchantillonnage 4x4) du pixel (px, py).
double squircleMaskAlpha(int px, int py, int size, double exponent = kSquircleExponent);

// Part des pixels du squircle qui sont opaques (alpha > 200) dans une image BGRA.
double squircleCoverage(const std::uint8_t* bgra, int w, int h, int stride);

// L'icône remplit-elle déjà la forme squircle (sinon : « icon jail » de Tahoe) ?
bool iconFitsSquircle(const std::uint8_t* bgra, int w, int h, int stride);

} // namespace md
