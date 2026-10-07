#include "apps_layout.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {
constexpr double kSearchTop = 44, kSearchH = 30, kSearchW = 280, kSearchGap = 40;
constexpr double kBottomRoom = 120, kDotsFromBottom = 90;   // place du Dock et des points
constexpr double kMinCellW = 120, kMinCellH = 128;
constexpr int kMaxColumns = 7, kMaxRows = 5;
} // namespace

AppsGeometry appsLayout(double screenW, double screenH, std::size_t count) {
    AppsGeometry g;
    g.searchTop = kSearchTop;
    g.searchH = kSearchH;
    g.searchW = std::min(kSearchW, std::max(0.0, screenW - 32));
    const double top = kSearchTop + kSearchH + kSearchGap;
    const double availH = std::max(1.0, screenH - kBottomRoom - top);
    const double margin = std::max(80.0, screenW * 0.1);
    const double availW = std::max(1.0, screenW - 2 * margin);
    g.columns = std::clamp(int(availW / kMinCellW), 1, kMaxColumns);
    g.rows = std::clamp(int(availH / kMinCellH), 1, kMaxRows);
    g.perPage = g.columns * g.rows;
    g.cellW = availW / g.columns;
    g.cellH = availH / g.rows;
    g.icon = std::clamp(std::min(g.cellW * 0.6, g.cellH - 40), 48.0, 96.0);
    g.gridLeft = (screenW - g.columns * g.cellW) / 2;
    g.gridTop = top;
    g.dotsY = screenH - kDotsFromBottom;
    g.pages = std::max(1, int((count + std::size_t(g.perPage) - 1) / std::size_t(g.perPage)));
    return g;
}

int pageOf(const AppsGeometry& g, int index) { return index < 0 ? 0 : index / std::max(1, g.perPage); }

int appsHit(const AppsGeometry& g, int page, double x, double y, std::size_t count) {
    if (x < g.gridLeft || y < g.gridTop || g.cellW <= 0 || g.cellH <= 0) return -1;
    const int col = int((x - g.gridLeft) / g.cellW), row = int((y - g.gridTop) / g.cellH);
    if (col >= g.columns || row >= g.rows) return -1;
    const long long i = (long long)page * g.perPage + (long long)row * g.columns + col;
    return i >= 0 && i < (long long)count ? int(i) : -1;
}

AppsCursor appsGoToPage(const AppsGeometry& g, AppsCursor c, int page, std::size_t count) {
    if (count == 0) return {0, -1};
    page = std::clamp(page, 0, g.pages - 1);
    return {page, c.selected < 0 ? -1 : std::min(page * g.perPage, int(count) - 1)};
}

AppsCursor appsKey(const AppsGeometry& g, AppsCursor c, UINT vk, std::size_t count) {
    if (count == 0) return {0, -1};
    const int n = int(count), last = n - 1;
    c.page = std::clamp(c.page, 0, g.pages - 1);
    const auto firstOf = [&](int page) { return std::min(page * g.perPage, last); };
    const bool arrow = vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN;
    if (arrow && c.selected < 0) return {c.page, firstOf(c.page)};   // première flèche : début de la page
    int s = std::clamp(c.selected, -1, last);
    switch (vk) {
        case VK_RIGHT: s = std::min(s + 1, last); break;
        case VK_LEFT: s = std::max(s - 1, 0); break;
        case VK_DOWN:
            if (s + g.columns <= last && pageOf(g, s + g.columns) == pageOf(g, s)) s += g.columns;
            break;
        case VK_UP:
            if (s - g.columns >= 0 && pageOf(g, s - g.columns) == pageOf(g, s)) s -= g.columns;
            break;
        case VK_NEXT:
        case VK_PRIOR: return appsGoToPage(g, {c.page, s}, c.page + (vk == VK_NEXT ? 1 : -1), count);
        case VK_HOME: s = 0; break;
        case VK_END: s = last; break;
        default: return c;
    }
    return {pageOf(g, s), s};
}

} // namespace md
