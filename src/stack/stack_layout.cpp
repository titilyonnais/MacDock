#include "stack_layout.h"

#include <algorithm>
#include <cmath>

#include "stack_model.h"

namespace md {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kFanGap = 10;         // espace vertical entre deux icônes de l'éventail (pt)
constexpr double kFanLift = 12;        // écart entre l'icône de la pile et la première icône
constexpr double kFanRadiusTiles = 55; // rayon de l'arc, en cases : courbure douce vers la droite
} // namespace

std::vector<FanSlot> fanLayout(std::size_t count, double tile) {
    count = std::min(count, kFanMaxItems);
    std::vector<FanSlot> out;
    out.reserve(count);
    const double radius = tile * kFanRadiusTiles;
    const double step = tile + kFanGap;
    for (std::size_t i = 0; i < count; ++i) {
        // Abscisse curviligne le long d'un cercle centré à droite : l'éventail monte et s'incline.
        double s = tile + kFanLift + double(i) * step;
        double theta = s / radius;
        out.push_back({radius * (1 - std::cos(theta)), -radius * std::sin(theta), theta * 180 / kPi});
    }
    return out;
}

std::size_t fanCapacity(double tile, double roomPt) {
    std::size_t n = 0;
    for (const FanSlot& s : fanLayout(kFanMaxItems, tile))
        if (-s.dy + tile / 2 <= roomPt) ++n;   // les emplacements montent : le premier qui dépasse arrête tout
        else break;
    return std::max<std::size_t>(n, 1);
}

GridGeometry gridLayout(std::size_t count, double tile, double maxHeight) {
    GridGeometry g;
    const std::size_t n = std::min(count, kGridMaxItems);
    g.iconSize = std::round(tile * 4 / 3);        // 64 pt pour des cases de 48
    g.cellWidth = g.iconSize + 32;
    g.cellHeight = g.iconSize + 44;               // icône, puis deux lignes de nom
    g.padding = 14;
    g.header = 40;
    g.footer = 34;
    if (n <= 4) g.columns = int(std::max<std::size_t>(n, 1));
    else g.columns = std::clamp(int(std::ceil(std::sqrt(double(n)))), 4, 5);
    g.rows = int((n + std::size_t(g.columns) - 1) / std::size_t(g.columns));
    const double chrome = g.header + g.footer + 2 * g.padding;
    int fit = int(std::floor((maxHeight - chrome) / g.cellHeight));
    g.visibleRows = std::min(g.rows, std::max(1, fit));
    g.width = g.columns * g.cellWidth + 2 * g.padding;
    g.height = chrome + g.visibleRows * g.cellHeight;
    return g;
}

} // namespace md
