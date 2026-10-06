// Modèle des menus contextuels en verre (logique pure, sans Win32) : entrées, mise en page en points,
// navigation au clavier et sélection à la souris.
#pragma once
#include <string>
#include <vector>

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

struct MenuItem {
    int id = 0;                    // 0 = séparateur
    std::wstring text;
    bool checked = false, enabled = true;
    std::vector<MenuItem> submenu;
    bool separator() const { return id == 0 && submenu.empty(); }
    bool selectable() const { return !separator() && enabled; }
};

struct MenuModel {
    std::vector<MenuItem> items;
};

struct MenuLayout {
    double width = 0, height = 0;
    std::vector<double> top;       // haut de chaque entrée, depuis le haut du panneau
};

// textWidthMax : largeur du texte le plus long (points), mesurée par l'appelant.
MenuLayout layoutMenu(const MenuModel& m, double textWidthMax);
// Entrée sélectionnable suivante (dir = +1) ou précédente (-1), en bouclant ; from = -1 pour partir d'un bord.
int nextSelectable(const MenuModel& m, int from, int dir);
// Entrée sélectionnable sous y (points depuis le haut du panneau), ou -1.
int hitTestMenu(const MenuLayout& l, const MenuModel& m, double y);

} // namespace md
