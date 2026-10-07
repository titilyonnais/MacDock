// Une barre par écran (logique pure) : ordre des écrans, écran actif, largeur d'une barre en points.
#pragma once
#include <windows.h>

#include <cstddef>
#include <vector>

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

} // namespace md
