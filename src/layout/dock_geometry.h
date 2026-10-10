// Géométrie du Dock dérivée de la grille d'icônes Apple (pur) : épaisseur, rayon concentrique, point, séparateur.
#pragma once
#include "../config/metrics.h"

namespace md {

struct DockGeometry {
    double thickness = 0;        // tile + 2·padding (fond au repos)
    double iconShape = 0;        // tile·iconShapeRatio (forme visible)
    double iconRadius = 0;       // iconShape·iconCornerRatio
    double visibleInset = 0;     // (tile − iconShape) / 2
    double cornerRadius = 0;     // fond : auto = iconRadius + padding + visibleInset, sinon dockCornerRadius ; ≤ thickness/2
    double indicatorCenter = 0;  // du bas du fond au centre du point
    double separatorLength = 0;  // thickness·separatorLengthRatio (0,738 de l'épaisseur, mesuré sur macOS 26)
    // De part et d'autre du trait, au-delà de l'écart entre cases : tile·separatorMarginRatio + separatorMarginOffset,
    // jamais négative (macOS 26 : le séparateur prend une demi-case plus 3 pt entre ses voisines).
    double separatorMargin = 0;
};

DockGeometry dockGeometry(double tileSize, const Metrics& m);

} // namespace md
