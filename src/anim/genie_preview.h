// Aperçu hors écran de l'effet génie : les mêmes bandes que l'animation réelle, dessinées sur le processeur
// (MacDock.exe --genie-snapshot, tests).
#pragma once
#include <cstdint>
#include <vector>

#include "../core/bgra_image.h"
#include "genie.h"

namespace md {

// Chaque bande de src (plus proche voisin) posée à sa destination, par-dessus dst.
void drawSlices(const BgraImage& src, const std::vector<GenieSlice>& slices, BgraImage& dst);
// Fenêtre factice opaque : barre de titre, barre latérale, lignes de texte.
BgraImage syntheticWindow(int w, int h);
// Planche 3 × 2 de vignettes 640 × 400 (t = 0 ; 0,2 ; 0,4 ; 0,6 ; 0,8 ; 1) : fond, Dock, case, fenêtre animée.
BgraImage genieSheet(MinimizeEffect e, DockPosition edge);

} // namespace md
