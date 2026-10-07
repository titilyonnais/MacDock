// Couleur du texte de la barre transparente selon le fond (logique pure).
#pragma once
#include <cstdint>

namespace md {

// Seuils de luminance relative linéaire (hystérésis) : texte foncé au-dessus, clair en dessous.
constexpr double kDarkTextAbove = 0.45;   // environ L* 73
constexpr double kLightTextBelow = 0.35;  // environ L* 66

double srgbToLinear(double c);   // composante sRGB 0..1 → linéaire 0..1
// Luminance relative moyenne d'une image BGRA8 sRGB (alpha ignoré).
double stripLuminance(const std::uint8_t* bgra, int w, int h, int strideBytes);
float halfToFloat(std::uint16_t h);
// Image RGBA16F scRGB (linéaire) : ramenée au blanc SDR (sdrWhite = 1 en SDR), bornée à 0..1 par pixel.
double stripLuminanceHalf(const std::uint16_t* rgba, int w, int h, int strideElems, double sdrWhite);
bool chooseDarkText(double luminance, bool currentlyDark);

} // namespace md
