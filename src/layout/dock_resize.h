// Poignée des séparateurs du Dock (logique pure, plan 70) : comme sur macOS 26, glisser un séparateur change la taille
// des icônes, un point de pointeur pour un point de taille, en s'éloignant du bord de l'écran pour l'agrandir.
#pragma once
#include "../config/settings.h"

namespace md {

// startAxis, nowAxis : position du pointeur en points sur l'axe perpendiculaire au bord du Dock (y pour un Dock en bas,
// x sur un côté). Bornée comme le réglage, de 16 à 128, et à maxTile : la plus grande taille qui tient sur l'axe.
double resizedTileSize(double startTile, double startAxis, double nowAxis, DockPosition edge, double maxTile = 128);

// Points entiers, arrondis vers la taille de départ : le tremblement d'un simple clic ne change rien.
double snappedTileSize(double startTile, double tile);

// Réglage pendant le glisser : setting (avant le glisser) tant que la taille n'a pas bougé de la taille affichée au départ
// (Dock plein : affiché plus petit que réglé), sinon la nouvelle taille en points entiers.
double resizedSetting(double setting, double startTile, double tile);

// Relâchement du bouton sur le Dock : le glisser de la poignée se termine avant tout (sinon un relâchement à avaler,
// celui d'un appui qui a fermé Spotlight, le laissait bloqué) ; puis le relâchement à avaler ; sinon le clic ou le
// glisser d'une icône.
enum class ButtonUp { EndResize, Swallow, Pointer };
ButtonUp leftButtonUpAction(bool resizing, bool swallowPending);

} // namespace md
