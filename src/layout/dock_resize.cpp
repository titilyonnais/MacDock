#include "dock_resize.h"

#include <algorithm>
#include <cmath>

namespace md {

double resizedTileSize(double startTile, double startAxis, double nowAxis, DockPosition edge) {
    constexpr double kMin = 16, kMax = 128;   // bornes du réglage (settingsFromJson)
    const double away = edge == DockPosition::Left ? nowAxis - startAxis : startAxis - nowAxis;
    const double tile = startTile + away;
    if (!std::isfinite(tile)) return std::clamp(std::isfinite(startTile) ? startTile : 48.0, kMin, kMax);
    return std::clamp(tile, kMin, kMax);
}

} // namespace md
