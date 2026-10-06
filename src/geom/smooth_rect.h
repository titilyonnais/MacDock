// Rectangles à coins continus façon Apple (« continuous corners ») : contour, masque, champ de distance (pur).
#pragma once
#include <vector>

namespace md {

struct Pt {
    double x = 0, y = 0;
};

constexpr double kCornerExtent = 1.52866483;   // le coin continu commence à 1,5287·r du sommet

// Rayon effectivement utilisable : min(r, min(w, h) / 2 / kCornerExtent), jamais négatif ni NaN.
double limitedCornerRadius(double w, double h, double r);

// Contour fermé (sens horaire, y vers le bas) d'un rectangle à coins continus façon Apple.
// Le premier point est (x + kCornerExtent·rl, y). stepsPerCurve >= 1.
std::vector<Pt> smoothRectOutline(double x, double y, double w, double h, double r, int stepsPerCurve = 12);

bool pointInPolygon(const std::vector<Pt>& poly, double x, double y);   // règle pair-impair
double polygonArea(const std::vector<Pt>& poly);                       // aire absolue (lacet)

// Couverture anticrénelée (4x4) d'un carré size x size contenant la forme de rayon cornerRatio·size.
std::vector<float> smoothSquareMask(int size, double cornerRatio);

// Champ de distance signée d'un coin de rayon 1, sur une grille n x n couvrant [lo, hi]² en coordonnées
// du coin (X vers l'intérieur le long du bord horizontal, Y vers l'intérieur le long du bord vertical,
// (0,0) = sommet). Négatif à l'intérieur. Cellule (i, j) au centre (lo + (i+0.5)·(hi-lo)/n, …), stockée [j*n+i].
std::vector<float> cornerDistanceField(int n, double lo, double hi);

// Lecture bilinéaire du champ (bornée aux bords), identique à l'échantillonnage du shader.
double sampleCornerField(const std::vector<float>& f, int n, double lo, double hi, double X, double Y);

} // namespace md
