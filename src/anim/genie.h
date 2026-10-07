// Effet génie et effet échelle (logique pure) : la fenêtre réduite est découpée en bandes, chacune posée à
// l'écran ; à l'écran, une miniature DWM par bande.
#pragma once
#include <windows.h>

#include <vector>

#include "../config/settings.h"

namespace md {

struct GenieSlice {
    RECT src;   // pixels de la fenêtre source
    RECT dst;   // pixels écran
};

// t = 0 : la fenêtre à sa place (from) ; t = 1 : dans la case du Dock (to). Bandes perpendiculaires à la
// direction du Dock (lignes de la source en bas, colonnes à gauche et à droite), de la plus éloignée à la plus
// proche du Dock, jointives. Vide pour l'effet Windows ou une source vide.
std::vector<GenieSlice> minimizeFrame(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t,
                                      int slices = 48);

// Durée en secondes (Maj enfoncée : ralenti × 8) ; 0 pour l'effet Windows.
double minimizeDuration(MinimizeEffect e, bool slow);

// Rectangle écran d'une fenêtre réduite avant sa réduction. rcNormalPosition est en coordonnées de la zone de
// travail (sauf fenêtre outil) ; réduite depuis l'état agrandi : la zone de travail, débordée des bordures
// invisibles (src plus grande, centrée).
RECT restoredRect(const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor, bool toolWindow, SIZE src);

} // namespace md
