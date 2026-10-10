#include "dock_resize.h"

#include <algorithm>
#include <cmath>

namespace md {

double resizedTileSize(double startTile, double startAxis, double nowAxis, DockPosition edge, double maxTile) {
    constexpr double kMin = 16;   // bornes du réglage (settingsFromJson) : 16 à 128
    const double kMax = std::clamp(std::isfinite(maxTile) ? maxTile : 128.0, kMin, 128.0);
    const double away = edge == DockPosition::Left ? nowAxis - startAxis : startAxis - nowAxis;
    const double tile = startTile + away;
    if (!std::isfinite(tile)) return std::clamp(std::isfinite(startTile) ? startTile : 48.0, kMin, kMax);
    return std::clamp(tile, kMin, kMax);
}

double snappedTileSize(double startTile, double tile) { return startTile + std::trunc(tile - startTile); }

ButtonUp leftButtonUpAction(bool resizing, bool swallowPending) {
    if (resizing) return ButtonUp::EndResize;
    return swallowPending ? ButtonUp::Swallow : ButtonUp::Pointer;
}

} // namespace md
