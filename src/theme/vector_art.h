// Petit dessin vectoriel sur le processeur (curseurs) : polygones remplis, bordés, avec une ombre douce.
#pragma once
#include <cstdint>
#include <utility>
#include <vector>

#include "../core/bgra_image.h"

namespace md {

struct Poly {
    std::vector<std::pair<double, double>> pts;   // unités d'un canevas de 32
};

struct Layer {
    std::vector<Poly> shapes;          // un point est dans la couche s'il est dans l'un des polygones (pair-impair)
    std::uint32_t fill = 0xFF000000;   // 0xAARRGGBB
    std::uint32_t outline = 0;         // 0xAARRGGBB, autour du remplissage
    double outlineWidth = 0;           // unités
};

// Couches dessinées dans l'ordre sur une image size × size ; coordonnées × unit ; sur-échantillonnage 4 × 4.
// shadow > 0 : ombre douce (alpha 0,3 × shadow) décalée vers le bas, sous l'ensemble.
BgraImage rasterize(const std::vector<Layer>& layers, int size, double unit, double shadow);

// Aides de construction.
Poly rotated(const Poly& p, double degrees, double cx, double cy);   // rotation autour de (cx, cy)
Poly translated(const Poly& p, double dx, double dy);
Poly circle(double cx, double cy, double r, int steps = 48);
Poly ring(double cx, double cy, double inner, double outer, int steps = 48);   // anneau (deux contours, pair-impair)
Poly rect(double x0, double y0, double x1, double y1);

} // namespace md
