// Géométrie des piles ouvertes : arc de l'éventail, panneau de la grille (points, logique pure).
#pragma once
#include <cstddef>
#include <vector>

namespace md {

struct FanSlot {
    double dx = 0, dy = 0;   // centre de l'icône, relatif au centre de l'icône de la pile (y vers le bas)
    double angle = 0;        // inclinaison en degrés (sens horaire)
};
// Emplacements de bas en haut ; au plus kFanMaxItems.
std::vector<FanSlot> fanLayout(std::size_t count, double tile);
// Nombre d'emplacements de l'éventail dont l'icône tient dans roomPt au-dessus du centre de la pile (1 au moins).
std::size_t fanCapacity(double tile, double roomPt);

struct GridGeometry {
    int columns = 0, rows = 0, visibleRows = 0;
    double width = 0, height = 0;            // panneau entier
    double cellWidth = 0, cellHeight = 0, iconSize = 0;
    double padding = 0, header = 0, footer = 0;   // marge intérieure, titre, lien « Ouvrir dans l'Explorateur »
};
// Au plus 5 colonnes et kGridMaxItems éléments ; visibleRows limitée par maxHeight (défilement au-delà).
GridGeometry gridLayout(std::size_t count, double tile, double maxHeight);

} // namespace md
