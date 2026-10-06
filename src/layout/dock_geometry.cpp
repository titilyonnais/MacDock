#include "dock_geometry.h"

#include <algorithm>
#include <cmath>

namespace md {

DockGeometry dockGeometry(double tileSize, const Metrics& m) {
    const double tile = std::isfinite(tileSize) ? std::max(0.0, tileSize) : 0.0;
    DockGeometry g;
    g.thickness = tile + 2 * m.dockPadding;
    g.iconShape = tile * m.iconShapeRatio;
    g.iconRadius = g.iconShape * m.iconCornerRatio;
    g.visibleInset = (tile - g.iconShape) / 2;
    // Concentrique : le fond suit le coin de l'icône, décalé de la marge visible entre l'icône et le bord.
    double r = m.dockCornerRadius > 0 ? m.dockCornerRadius : g.iconRadius + m.dockPadding + g.visibleInset;
    g.cornerRadius = std::clamp(r, 0.0, g.thickness / 2);
    g.indicatorCenter = m.indicatorCenterFromBottom;
    g.separatorLength = tile * m.separatorLengthRatio;
    return g;
}

} // namespace md
