// Tuiles colorées des sections de l'app Réglages, avec leur pictogramme blanc dessiné en Direct2D (aucune ressource
// Apple ni symbole SF) : engrenage, Dock, barre des menus, fenêtres, bureaux, clavier, capture, son, police, mod, info.
#pragma once
#include <d2d1.h>

#include <cstdint>

#include "../ui/ui_draw.h"
#include "panes.h"

namespace md {

// Tuile de r (20 × 20 pt conseillé) : rectangle arrondi de `color` (0xRRGGBB), léger dégradé, pictogramme blanc.
// `cornerRatio` : rayon / côté (0 : celui des tuiles de la barre latérale).
void drawPaneTile(ui::Painter& p, D2D1_RECT_F r, std::uint32_t color, PaneIcon icon, float cornerRatio = 0);

}  // namespace md
