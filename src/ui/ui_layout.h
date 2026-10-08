// Mise en page de l'app Réglages (logique pure, en points) : groupes et lignes d'une section, lignes de la barre
// latérale, et calculs des contrôles (curseur, segments, menu, ordre du focus).
#pragma once
#include <vector>

#include "ui_theme.h"

namespace md::ui {

struct GroupShape {
    bool title = false;          // titre au-dessus du groupe
    std::vector<float> rows;     // hauteur de chaque ligne
    bool footer = false;         // note sous le groupe
};
struct RowBox {
    float top = 0, height = 0;
};
struct GroupBox {
    float titleTop = 0;          // haut du titre (si titre)
    float top = 0, height = 0;   // le groupe arrondi
    std::vector<RowBox> rows;
    float footerTop = 0;         // haut de la note (si note)
};
struct PaneLayout {
    std::vector<GroupBox> groups;
    float height = 0;            // hauteur totale du contenu (pour le défilement)
};

// Positions verticales depuis le haut de la fenêtre (avant défilement).
PaneLayout layoutPane(const std::vector<GroupShape>& groups);

// Curseur : position du centre du bouton pour une valeur, et valeur (au pas près, bornée) pour une abscisse.
float sliderKnobX(double value, double min, double max, float left, float right);
double sliderValueAt(float x, double min, double max, double step, float left, float right);
// Contrôle segmenté de largeurs égales : segment sous x, ou -1.
int segmentAt(float x, float left, float right, int count);
// Menu ouvert : élément sous y (marge du haut `top`, éléments de `itemHeight`), ou -1.
int menuItemAt(float y, float top, float itemHeight, int count);
// Tab (ou Maj+Tab) : prochain élément disponible en bouclant ; -1 si aucun.
int nextFocus(int current, const std::vector<bool>& focusable, bool backwards);

// Panneau de verre de la barre latérale (Tahoe) : rectangle arrondi en retrait, dans une fenêtre de cette hauteur.
struct Panel {
    float left = 0, top = 0, right = 0, bottom = 0, radius = 0;
};
Panel sidebarPanel(float windowHeight);
bool insidePanel(const Panel& p, float x, float y);   // coins arrondis compris

// Barre latérale : haut de chaque ligne, sections en groupes de `sizes` lignes.
std::vector<float> sidebarRowTops(const std::vector<int>& sizes);
int sidebarRowAt(float y, const std::vector<float>& tops);   // ligne sous y, ou -1

}  // namespace md::ui
