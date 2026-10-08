// Modèle des menus contextuels en verre (logique pure, sans Win32) : entrées, mise en page en points,
// navigation au clavier et sélection à la souris.
#pragma once
#include <windows.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../icons/icon_provider.h"
#include "glyph_kind.h"

namespace md {

// Mesures des menus de macOS Tahoe (estimées), en points.
constexpr double kMenuItemHeight = 24;
constexpr double kMenuSeparatorHeight = 11;   // trait de 1 pt et 5 pt de marge de chaque côté
constexpr double kMenuPadding = 5;            // marge intérieure du panneau
constexpr double kMenuTextLeft = 20;          // place de la coche
constexpr double kMenuTextLeftCompact = 11;   // menu sans coche : texte à ~15 pt du bord (macOS 27)
constexpr double kMenuTextRight = 28;         // place de la flèche de sous-menu
constexpr double kMenuRadius = 12;            // rayon du panneau
constexpr double kMenuHighlightRadius = 6;    // rayon de la capsule de survol
constexpr double kMenuFontSize = 13;
constexpr double kMenuMinWidth = 160;
constexpr double kMenuIconSize = 16;          // icône d'entrée (liste d'une pile)
constexpr double kMenuIconGap = 6;
constexpr double kMenuShortcutGap = 24;     // entre le texte le plus long et le raccourci le plus long

// Lignes enrichies (menus d'état de la barre, Centre de contrôle), comme les NSMenuItem à vue de macOS.
enum class MenuRow {
    Normal,   // entrée ordinaire (ou séparateur)
    Header,   // intitulé de section : petit texte gris en gras, inerte
    Slider,   // curseur (value, 0..1), pictogramme glyph à gauche ; agit en direct
    Toggle,   // texte et interrupteur (on) à droite
    Tiles,    // rangée de tuiles (tiles)
    Media,    // lecture en cours : text (titre), subtitle (artiste), boutons précédent, lecture/pause, suivant
};
constexpr double kMenuHeaderHeight = 22;
constexpr double kMenuSliderHeight = 30;
constexpr double kMenuToggleHeight = 26;
constexpr double kMenuTilesHeight = 62;
constexpr double kMenuMediaHeight = 50;
constexpr double kMenuSliderLeft = 36;     // début de la piste (après le pictogramme), depuis le bord de la ligne
constexpr double kMenuSliderRight = 14;
constexpr double kMenuTileInset = 4;       // marge des tuiles dans la ligne
constexpr double kMenuTileGap = 8;
constexpr double kMenuMediaButton = 28;    // largeur d'un bouton de lecture
constexpr double kMenuMediaRight = 8;

struct MenuTile {
    std::wstring title, subtitle;
    Glyph glyph = Glyph::None;
    bool on = false, enabled = true;
};

struct MenuItem {
    int id = 0;                    // 0 = séparateur
    std::wstring text;
    bool checked = false, enabled = true;
    std::vector<MenuItem> submenu;
    IconProvider::ImagePtr icon;   // facultative : affichée devant le texte
    std::wstring shortcut;         // texte du raccourci, aligné à droite en gris (« Ctrl+S »)
    MenuRow row = MenuRow::Normal;
    double value = 0;              // curseur
    bool on = false;               // interrupteur
    Glyph glyph = Glyph::None;     // pictogramme du curseur
    float level = 1;               // niveau du pictogramme (ondes du haut-parleur…)
    std::vector<MenuTile> tiles;
    std::wstring subtitle;         // média : artiste
    bool playing = false;          // média
    bool separator() const { return row == MenuRow::Normal && id == 0 && submenu.empty(); }
    // Entrée ordinaire, au clavier ou à la souris ; les lignes enrichies ne réagissent qu'à la souris.
    bool selectable() const { return row == MenuRow::Normal && !separator() && enabled; }
};

struct MenuModel {
    std::vector<MenuItem> items;
    double width = 0;   // points ; 0 = selon le texte
};

double menuRowHeight(MenuRow r);
// Valeur du curseur sous x (points depuis le bord gauche de la ligne), bornée à [0, 1].
double sliderValueAt(double rowWidth, double x);
int tileAt(std::size_t tiles, double rowWidth, double x);   // -1 hors des tuiles
int mediaButtonAt(double rowWidth, double x);              // 0 précédent, 1 lecture/pause, 2 suivant, -1
// Rafraîchissement d'un menu ouvert : refresh modifie une copie ; seules les valeurs sont reprises (jamais la
// structure), et le curseur en cours de glissement (draggingId) garde la sienne. true si le modèle a été repris.
bool applyRefresh(MenuModel& m, const std::function<bool(MenuModel&)>& refresh, int draggingId);

struct MenuLayout {
    double width = 0, height = 0;
    std::vector<double> top;       // haut de chaque entrée, depuis le haut du panneau
    double iconSpace = 0;          // place des icônes devant le texte (0 si aucune entrée n'en a)
    double textLeft = kMenuTextLeft;   // colonne de coche, réduite dans un menu sans coche
};

// textWidthMax, shortcutWidthMax : largeurs du texte et du raccourci les plus longs (points), mesurées par l'appelant.
MenuLayout layoutMenu(const MenuModel& m, double textWidthMax, double shortcutWidthMax = 0);
// Entrée sélectionnable suivante (dir = +1) ou précédente (-1), en bouclant ; from = -1 pour partir d'un bord.
int nextSelectable(const MenuModel& m, int from, int dir);
// Entrée sélectionnable sous y (points depuis le haut du panneau), ou -1.
int hitTestMenu(const MenuLayout& l, const MenuModel& m, double y);
// Ligne sous y, sélectionnable ou non (séparateurs exclus), ou -1.
int rowAt(const MenuLayout& l, const MenuModel& m, double y);

// Barre de menus : pendant qu'un menu est ouvert, survoler un autre titre (ou flèche gauche/droite) le ferme
// et MenuWindow::track renvoie menuSwitchResult(k) pour ouvrir le titre k.
constexpr int kMenuSwitchBase = -1000;
constexpr int menuSwitchResult(int k) { return kMenuSwitchBase - k; }
std::optional<int> menuSwitchTarget(int result);
// Titre (rectangles écran) sous pt autre que current, sinon -1.
int barTitleAt(const std::vector<RECT>& titles, POINT pt, int current);

} // namespace md
