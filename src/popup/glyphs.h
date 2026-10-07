// Pictogrammes de la barre de menus et des menus d'état, dessinés en Direct2D (aucune ressource Apple ni symbole SF) :
// haut-parleur, Wi-Fi, batterie, Centre de contrôle, lecture…
#pragma once
#include <windows.h>
#include <d2d1.h>

#include "glyph_kind.h"

namespace md {

// Dessine g dans box (carré conseillé) avec ink ; les parties éteintes (arcs du Wi-Fi) sont à 30 % d'opacité.
void drawGlyph(ID2D1RenderTarget* rt, Glyph g, D2D1_RECT_F box, ID2D1Brush* ink, float level = 1, bool alt = false);

} // namespace md
