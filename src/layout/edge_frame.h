// Repère local du Dock et fenêtre réelle, selon le bord de l'écran (bas, gauche, droite).
// Le contrôleur calcule toujours « comme en bas » : x le long du Dock, y croissant vers le bord de l'écran
// (y = cross() contre le bord). EdgeFrame convertit dans les deux sens (fonctions pures).
#pragma once
#include <windows.h>

#include "../config/settings.h"

namespace md {

struct EdgePoint {
    double x = 0, y = 0;
};

struct EdgeFrame {
    DockPosition edge = DockPosition::Bottom;
    double width = 0, height = 0;   // fenêtre, en pixels

    bool vertical() const { return edge != DockPosition::Bottom; }
    double axis() const { return vertical() ? height : width; }    // longueur le long du Dock
    double cross() const { return vertical() ? width : height; }   // épaisseur de la fenêtre
    POINT toLocal(POINT window) const;
    EdgePoint toWindow(double x, double y) const;
};

} // namespace md
