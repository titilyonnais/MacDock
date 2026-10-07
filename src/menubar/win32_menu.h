// Vrais menus des apps : entrées lues (modèle commun aux menus Win32 et UI Automation) et lecture des menus Win32
// (HMENU d'une fenêtre, lisible depuis un autre processus).
#pragma once
#include <windows.h>

#include <string>
#include <string_view>
#include <vector>

namespace md {

struct RawMenuItem {
    std::wstring text, shortcut;
    UINT id = 0;                 // Win32 : identifiant envoyé par WM_COMMAND
    int position = -1;           // position dans le menu parent (entrées omises comprises)
    bool popup = false;          // ouvre un sous-menu (children, éventuellement vide)
    bool separator = false, enabled = true, checked = false;
    std::vector<RawMenuItem> children;
};

struct MenuLabel {
    std::wstring text, shortcut;
};
// « &Enregistrer\tCtrl+S » → « Enregistrer », « Ctrl+S » ; « && » → « & ».
MenuLabel parseMenuLabel(std::wstring_view raw);

// Entrées d'un menu, sous-menus compris jusqu'à maxDepth niveaux. Les entrées sans texte lisible (owner-draw,
// image) sont omises.
std::vector<RawMenuItem> readWin32Menu(HMENU menu, int maxDepth = 4);
// Titres d'une barre de menus (sans leurs entrées).
std::vector<RawMenuItem> win32MenuTitles(HMENU bar);
// Laisse l'app préparer le menu du titre à cette position avant sa lecture (coches, entrées grisées, fichiers
// récents) : WM_INITMENU, puis WM_INITMENUPOPUP pour le menu et ses sous-menus. 200 ms au plus par message et
// 500 ms en tout ; une app qui ne répond pas est abandonnée. false si l'app n'a pas répondu.
bool refreshWin32Popup(HWND owner, HMENU bar, int position);
// Au moins une entrée lisible sous l'un des titres (sinon : menus génériques plutôt que des menus vides).
bool hasReadableEntries(const std::vector<RawMenuItem>& titles);
// Relit les titres de la barre (l'app a pu la changer : MDI, document ouvert) ; real reçoit les nouveaux titres,
// avec les entrées déjà lues des titres inchangés. true si la barre a changé.
bool syncWin32Titles(HMENU bar, std::vector<RawMenuItem>& real);

} // namespace md
