// « Déplacer et redimensionner » de macOS 26 (plan 50) : ranger une fenêtre sans la glisser — moitiés, quarts,
// Remplir, Centrer, revenir à la taille précédente — avec les marges de macOS (8 pt par défaut).
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace md {

enum class TileAction { Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight, Fill, Center, Previous };

// Nom dans les actions des menus (« left », « top-right », « fill »…) ; nullopt si inconnu.
std::optional<TileAction> parseTileAction(std::wstring_view name);
std::wstring tileActionName(TileAction a);

// Cadre visible voulu (pixels) dans la zone de travail `work`, avec `margin` au bord et entre deux fenêtres. Center
// garde la taille du cadre actuel `current` (bornée à la zone) ; Previous rend `current` (le cadre d'avant est gardé par
// tileWindow).
RECT tileRect(TileAction a, const RECT& work, const RECT& current, int margin);
// Cadre obtenu `got` différent du cadre voulu `target` (taille minimale de l'app) : même taille, recalé contre le bord
// que vise l'action (droite pour Droite et les quarts de droite, bas pour Bas…), sinon contre le haut et la gauche.
RECT anchorTile(TileAction a, const RECT& target, const RECT& got);

// Range la fenêtre : son cadre visible, sans les bordures invisibles, est calé sur tileRect dans la zone de travail de
// son écran (agrandie : restaurée d'abord ; taille minimale de l'app : recalée par anchorTile). Le cadre d'avant le
// premier rangement est gardé pour Previous. false, sans rien changer : fenêtre disparue, réduite ou figée, taille fixe
// (seul Centrer la déplace), refus de Windows, ou rien à quoi revenir. `marginPt` : marge au bord et entre deux fenêtres
// (8 pt, celle des menus de macOS ; 0 pour Remplir depuis l'agrandissement, plan 72).
bool tileWindow(HWND window, TileAction a, int marginPt = 8);
// Fenêtre rangée en dernier par `a` et restée à ce cadre visible (plan 72 : une fenêtre remplie qu'on agrandit de
// nouveau revient à sa taille d'avant, comme le double-clic de macOS).
bool tiledBy(HWND window, TileAction a, const RECT& frame);
// Cadre visible de la fenêtre (DWMWA_EXTENDED_FRAME_BOUNDS, sinon GetWindowRect), pixels écran.
RECT windowFrame(HWND window);
// Fenêtre agrandie rendue à sa taille d'avant sans l'animation de Windows (celle d'agrandir vient de jouer) ; true si
// elle n'est plus agrandie (déjà rendue : rien à faire).
bool restoreWithoutAnimation(HWND window);

// Plein écran de macOS (pastille verte, plan 72) : cadre visible calé sur l'écran entier de la fenêtre (la barre des
// menus et le Dock se cachent d'eux-mêmes devant une fenêtre qui le couvre), cadre et état agrandi d'avant gardés ; de
// nouveau : rendus. false, sans rien changer : fenêtre disparue, réduite, figée ou refus de Windows.
bool toggleFullscreen(HWND window);
// Après toggleFullscreen : la fenêtre a été agrandie de nouveau par la sortie (elle l'était avant le plein écran).
bool fullscreenExitMaximized(HWND window);
// Mise en plein écran par MacDock et couvre toujours son écran.
bool fullscreenByMacDock(HWND window);

// « Organiser » de macOS 26 (plan 51) : la fenêtre choisie et les suivantes rangées ensemble.
enum class Arrangement { LeftRight, RightLeft, TopBottom, BottomTop, Quarters };
// « left-right », « right-left », « top-bottom », « bottom-top », « quarters » ; nullopt si inconnu.
std::optional<Arrangement> parseArrangement(std::wstring_view name);
std::wstring arrangementName(Arrangement a);
// Places dans l'ordre : la fenêtre choisie prend la première, les suivantes les autres.
std::vector<TileAction> arrangementSlots(Arrangement a);

// Fenêtre de premier niveau dans l'ordre d'affichage (de l'avant vers l'arrière).
struct ArrangeCandidate {
    HWND window = nullptr;
    bool eligible = false;   // visible, non réduite, sur ce bureau, à barre de titre, redimensionnable, ni outil ni dialogue
    HMONITOR monitor = nullptr;
    bool topmost = false;    // « toujours au-dessus » (épinglée)
};
// Fenêtres à organiser : `first` (même inadmissible ou absente de la liste), puis les admissibles de l'écran
// `monitor`, de l'avant vers l'arrière, comme macOS ; celles « toujours au-dessus » après les autres (avant, si
// `first` l'est aussi).
std::vector<HWND> arrangeCandidates(const std::vector<ArrangeCandidate>& zOrder, HWND first, HMONITOR monitor);
// Range `first` à la première place, puis chaque place suivante avec la prochaine fenêtre qui l'accepte (une fenêtre
// élevée, figée ou qui ne répond pas en 100 ms est sautée). Chaque fenêtre rangée passe juste sous la précédente, comme
// sur macOS. false si `first` n'a pas pu être rangée (rien n'est fait).
bool arrangeWindows(HWND first, Arrangement a);

} // namespace md
