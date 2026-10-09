// « Déplacer et redimensionner » de macOS 26 (plan 50) : ranger une fenêtre sans la glisser — moitiés, quarts,
// Remplir, Centrer, revenir à la taille précédente — avec les marges de macOS (8 pt par défaut).
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace md {

enum class TileAction { Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Fill, Center, Previous };

// Nom dans les actions des menus (« left », « top-right », « fill »…) ; nullopt si inconnu.
std::optional<TileAction> parseTileAction(std::wstring_view name);
std::wstring tileActionName(TileAction a);

// Cadre visible voulu (pixels) dans la zone de travail `work`, avec `margin` au bord et entre deux fenêtres. Center
// garde la taille du cadre actuel `current` (bornée à la zone) ; Previous rend `current` (le cadre d'avant est gardé par
// tileWindow).
RECT tileRect(TileAction a, const RECT& work, const RECT& current, int margin);

// Range la fenêtre (restaurée d'abord si elle est agrandie ou réduite) : son cadre visible, sans les bordures
// invisibles, est calé sur tileRect dans la zone de travail de son écran. Le cadre d'avant le premier rangement est
// gardé pour Previous. false : fenêtre disparue, ou rien à quoi revenir.
bool tileWindow(HWND window, TileAction a);

} // namespace md
