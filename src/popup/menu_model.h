// Modèle des menus contextuels en verre (logique pure, sans Win32) : entrées, mise en page en points,
// navigation au clavier et sélection à la souris.
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "../icons/icon_provider.h"

namespace md {

// Mesures des menus de macOS Tahoe (estimées), en points.
constexpr double kMenuItemHeight = 24;
constexpr double kMenuSeparatorHeight = 11;   // trait de 1 pt et 5 pt de marge de chaque côté
constexpr double kMenuPadding = 5;            // marge intérieure du panneau
constexpr double kMenuTextLeft = 20;          // place de la coche
constexpr double kMenuTextRight = 28;         // place de la flèche de sous-menu
constexpr double kMenuRadius = 12;            // rayon du panneau
constexpr double kMenuHighlightRadius = 6;    // rayon de la capsule de survol
constexpr double kMenuFontSize = 13;
constexpr double kMenuMinWidth = 160;
constexpr double kMenuIconSize = 16;          // icône d'entrée (liste d'une pile)
constexpr double kMenuIconGap = 6;
constexpr double kMenuShortcutGap = 24;     // entre le texte le plus long et le raccourci le plus long

struct MenuItem {
    int id = 0;                    // 0 = séparateur
    std::wstring text;
    bool checked = false, enabled = true;
    std::vector<MenuItem> submenu;
    IconProvider::ImagePtr icon;   // facultative : affichée devant le texte
    std::wstring shortcut;         // texte du raccourci, aligné à droite en gris (« Ctrl+S »)
    bool separator() const { return id == 0 && submenu.empty(); }
    bool selectable() const { return !separator() && enabled; }
};

struct MenuModel {
    std::vector<MenuItem> items;
};

struct MenuLayout {
    double width = 0, height = 0;
    std::vector<double> top;       // haut de chaque entrée, depuis le haut du panneau
    double iconSpace = 0;          // place des icônes devant le texte (0 si aucune entrée n'en a)
};

// textWidthMax, shortcutWidthMax : largeurs du texte et du raccourci les plus longs (points), mesurées par l'appelant.
MenuLayout layoutMenu(const MenuModel& m, double textWidthMax, double shortcutWidthMax = 0);
// Entrée sélectionnable suivante (dir = +1) ou précédente (-1), en bouclant ; from = -1 pour partir d'un bord.
int nextSelectable(const MenuModel& m, int from, int dir);
// Entrée sélectionnable sous y (points depuis le haut du panneau), ou -1.
int hitTestMenu(const MenuLayout& l, const MenuModel& m, double y);

// Barre de menus : pendant qu'un menu est ouvert, survoler un autre titre (ou flèche gauche/droite) le ferme
// et MenuWindow::track renvoie menuSwitchResult(k) pour ouvrir le titre k.
constexpr int kMenuSwitchBase = -1000;
constexpr int menuSwitchResult(int k) { return kMenuSwitchBase - k; }
std::optional<int> menuSwitchTarget(int result);
// Titre (rectangles écran) sous pt autre que current, sinon -1.
int barTitleAt(const std::vector<RECT>& titles, POINT pt, int current);

} // namespace md
