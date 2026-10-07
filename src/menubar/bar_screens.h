// Une barre par écran (logique pure) : ordre des écrans, écran actif, largeur d'une barre en points.
#pragma once
#include <windows.h>

#include <cstddef>
#include <vector>

#include "bar_layout.h"

namespace md {

struct ScreenInfo {
    RECT rect{};
    UINT dpi = 96;
    bool primary = false;
};

std::vector<ScreenInfo> orderScreens(std::vector<ScreenInfo> s);   // principal d'abord, puis de gauche à droite
// Écran actif : celui qui contient le plus de la fenêtre au premier plan ; sans fenêtre (bureau) ou hors de tout
// écran, celui du curseur ; sinon le premier (principal).
std::size_t activeScreen(const std::vector<ScreenInfo>& s, const RECT* foreground, POINT cursor);
double barWidthPoints(const ScreenInfo& s);

// Écrans branchés ou débranchés : pour chaque écran voulu, l'index de la barre existante gardée (même rectangle) ou -1
// (barre à créer) ; drop : barres existantes à détruire. La nouvelle liste est complète avant toute fenêtre créée.
struct ScreenPlan {
    std::vector<int> keep;
    std::vector<std::size_t> drop;
};
ScreenPlan planScreens(const std::vector<RECT>& existing, const std::vector<ScreenInfo>& wanted);

// Refaire les écrans : jamais pendant un menu ni imbriqué (WM_DISPLAYCHANGE peut arriver pendant un SendMessage).
struct RebuildGate {
    bool pending = false, running = false;
    bool tryBegin(bool blocked);   // false : demande notée, à refaire plus tard
    bool end();                    // true : une demande est arrivée entre-temps
};

// Curseur au bord haut de cet écran (pas d'un écran placé au-dessus), ou sur sa barre de height pixels.
bool cursorAtTopEdge(const RECT& screen, POINT pt);
bool cursorInBar(const RECT& screen, int height, POINT pt);

// Nombre d'icônes d'apps (largeur trayWidth) qui tiennent à gauche des icônes système sans toucher les titres
// toujours visibles (in.keepLeft premiers) ; in.rightWidths : icônes système et horloge seulement.
std::size_t trayFit(const BarLayoutInput& in, std::size_t trayCount, double trayWidth);

} // namespace md
