// Poignée des séparateurs du Dock (logique pure, plan 70) : comme sur macOS 26, glisser un séparateur change la taille
// des icônes, un point de pointeur pour un point de taille, en s'éloignant du bord de l'écran pour l'agrandir.
#pragma once
#include "../config/settings.h"

namespace md {

// startAxis, nowAxis : position du pointeur en points sur l'axe perpendiculaire au bord du Dock (y pour un Dock en bas,
// x sur un côté). Bornée comme le réglage, de 16 à 128.
double resizedTileSize(double startTile, double startAxis, double nowAxis, DockPosition edge);

} // namespace md
