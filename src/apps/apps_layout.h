// Géométrie de l'écran Apps (points) et déplacement au clavier : logique pure.
#pragma once
#include <windows.h>

#include <cstddef>

namespace md {

struct AppsGeometry {
    int columns = 1, rows = 1, perPage = 1, pages = 1;
    double cellW = 0, cellH = 0, icon = 0;   // case et icône
    double gridLeft = 0, gridTop = 0;        // coin de la grille
    double searchTop = 0, searchW = 0, searchH = 0;   // champ de recherche, centré
    double dotsY = 0;                        // centre des points de pages
};

// Au plus 7 × 5 cases par page ; icône de 48 à 96 pt.
AppsGeometry appsLayout(double screenW, double screenH, std::size_t count);
constexpr double kAppsLabelH = 32, kAppsLabelGap = 6;   // nom sous l'icône, deux lignes au plus
struct AppsBox {
    double left = 0, top = 0, right = 0, bottom = 0;
};
// Icône et nom de la case `slot` de la page : le halo de survol, et la seule zone qui répond au clic (sur un grand
// écran, une case est bien plus large que l'icône : un clic entre deux icônes ne lance rien).
AppsBox appsItemRect(const AppsGeometry& g, int slot);
// Indice (dans la liste affichée) de l'app sous (x, y) sur la page, ou -1 (hors d'une icône et de son nom, case vide).
int appsHit(const AppsGeometry& g, int page, double x, double y, std::size_t count);
int pageOf(const AppsGeometry& g, int index);

struct AppsCursor {
    int page = 0;
    int selected = -1;   // -1 : aucune
};
// Aller à une page (molette, points) : une sélection passe à la première app de cette page.
AppsCursor appsGoToPage(const AppsGeometry& g, AppsCursor c, int page, std::size_t count);

// Un clic n'agit que s'il a commencé dans la vue : le relâchement d'un clic commencé ailleurs (second clic d'un
// double-clic sur le bouton Apps) est ignoré.
struct PressGate {
    bool down = false;
    void press() { down = true; }
    bool release() {
        const bool was = down;
        down = false;
        return was;
    }
};

// Flèches, Page précédente / suivante, Début, Fin ; la page suit la sélection.
AppsCursor appsKey(const AppsGeometry& g, AppsCursor c, UINT vk, std::size_t count);

} // namespace md
