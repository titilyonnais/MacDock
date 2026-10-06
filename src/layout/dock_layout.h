// Mise en page du Dock et magnification (fonctions pures).
// Toutes les grandeurs sont en points, sur l'axe principal du Dock
// (horizontal en bas, vertical à gauche/droite), origine au centre du Dock au repos.
#pragma once
#include <optional>
#include <vector>

namespace md {

struct LayoutItemSpec {
    bool separator = false;
};

struct LayoutInput {
    std::vector<LayoutItemSpec> items;
    double tileSize = 48;
    double largeSize = 128;
    double gap = 6;
    double padding = 8;
    double separatorWidth = 1;
    double separatorMargin = 7;
    double rangeTiles = 3.0;
    double amount = 0;              // 0..1 : intensité animée de la magnification
    std::optional<double> cursor;   // position du curseur sur l'axe principal
};

struct LayoutItem {
    double center = 0;
    double size = 0;   // côté de l'icône, ou largeur du trait pour un séparateur
};

struct LayoutResult {
    std::vector<LayoutItem> items;
    double bgStart = 0, bgEnd = 0;   // bornes du fond sur l'axe principal
    double restLength = 0;           // longueur du fond au repos
    double thickness = 0;            // épaisseur du fond au repos = tile + 2*padding
    double maxSize = 0;              // plus grande icône
};

// Taille d'une icône à une distance donnée du curseur (cosinus surélevé).
double magnifiedSize(double distance, double tile, double large, double range);

LayoutResult computeLayout(const LayoutInput& in);

} // namespace md
