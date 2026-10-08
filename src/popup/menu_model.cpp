#include "menu_model.h"

#include <algorithm>
#include <cmath>

namespace md {

namespace {
double heightOf(const MenuItem& it) { return it.separator() ? kMenuSeparatorHeight : menuRowHeight(it.row); }
} // namespace

double menuRowHeight(MenuRow r) {
    switch (r) {
        case MenuRow::Header: return kMenuHeaderHeight;
        case MenuRow::Slider: return kMenuSliderHeight;
        case MenuRow::Toggle: return kMenuToggleHeight;
        case MenuRow::Tiles: return kMenuTilesHeight;
        case MenuRow::Media: return kMenuMediaHeight;
        case MenuRow::Normal: break;
    }
    return kMenuItemHeight;
}

double sliderValueAt(double rowWidth, double x) {
    const double span = rowWidth - kMenuSliderLeft - kMenuSliderRight;
    if (!(span > 0) || !std::isfinite(x)) return 0;
    return std::clamp((x - kMenuSliderLeft) / span, 0.0, 1.0);
}

int tileAt(std::size_t tiles, double rowWidth, double x) {
    if (tiles == 0) return -1;
    const double inner = rowWidth - 2 * kMenuTileInset;
    const double w = (inner - double(tiles - 1) * kMenuTileGap) / double(tiles);
    if (w <= 0) return -1;
    const double rel = x - kMenuTileInset;
    if (rel < 0) return -1;
    const int k = int(rel / (w + kMenuTileGap));
    if (k >= int(tiles) || rel - k * (w + kMenuTileGap) > w) return -1;
    return k;
}

int mediaButtonAt(double rowWidth, double x) {
    const double right = rowWidth - kMenuMediaRight, left = right - 3 * kMenuMediaButton;
    if (x < left || x >= right) return -1;
    return int((x - left) / kMenuMediaButton);
}

bool applyRefresh(MenuModel& m, const std::function<bool(MenuModel&)>& refresh, int draggingId) {
    if (!refresh) return false;
    MenuModel copy = m;
    if (!refresh(copy) || copy.items.size() != m.items.size()) return false;
    for (std::size_t i = 0; i < m.items.size(); ++i)
        if (copy.items[i].row != m.items[i].row || copy.items[i].id != m.items[i].id ||
            copy.items[i].tiles.size() != m.items[i].tiles.size())
            return false;
    for (std::size_t i = 0; i < m.items.size(); ++i)
        if (draggingId != 0 && m.items[i].id == draggingId) copy.items[i].value = m.items[i].value;
    copy.width = m.width;
    m = std::move(copy);
    return true;
}

MenuLayout layoutMenu(const MenuModel& m, double textWidthMax, double shortcutWidthMax) {
    MenuLayout l;
    double y = kMenuPadding;
    for (auto& it : m.items) {
        l.top.push_back(y);
        y += heightOf(it);
    }
    l.height = y + kMenuPadding;
    double text = std::isfinite(textWidthMax) ? std::max(0.0, textWidthMax) : 0.0;
    if (std::any_of(m.items.begin(), m.items.end(), [](const MenuItem& it) { return it.icon != nullptr; }))
        l.iconSpace = kMenuIconSize + kMenuIconGap;
    if (std::none_of(m.items.begin(), m.items.end(), [](const MenuItem& it) { return it.checked; }))
        l.textLeft = kMenuTextLeftCompact;
    double shortcut = std::isfinite(shortcutWidthMax) && shortcutWidthMax > 0 ? kMenuShortcutGap + shortcutWidthMax : 0.0;
    l.width = std::max(kMenuMinWidth,
                       std::ceil(2 * kMenuPadding + l.textLeft + l.iconSpace + text + shortcut + kMenuTextRight));
    if (m.width > 0) l.width = m.width;
    return l;
}

int nextSelectable(const MenuModel& m, int from, int dir) {
    const int n = int(m.items.size());
    if (n == 0) return -1;
    dir = dir < 0 ? -1 : 1;
    int i = from;
    if (i < 0 || i >= n) i = dir > 0 ? -1 : n;
    for (int step = 0; step < n; ++step) {
        i = ((i + dir) % n + n) % n;
        if (m.items[size_t(i)].selectable()) return i;
    }
    return -1;
}

int hitTestMenu(const MenuLayout& l, const MenuModel& m, double y) {
    for (size_t i = 0; i < m.items.size() && i < l.top.size(); ++i) {
        const MenuItem& it = m.items[i];
        if (y >= l.top[i] && y < l.top[i] + heightOf(it)) return it.selectable() ? int(i) : -1;
    }
    return -1;
}

int rowAt(const MenuLayout& l, const MenuModel& m, double y) {
    for (size_t i = 0; i < m.items.size() && i < l.top.size(); ++i) {
        const MenuItem& it = m.items[i];
        if (y >= l.top[i] && y < l.top[i] + heightOf(it)) return it.separator() ? -1 : int(i);
    }
    return -1;
}

std::optional<int> menuSwitchTarget(int result) {
    if (result > kMenuSwitchBase) return std::nullopt;
    return kMenuSwitchBase - result;
}

int barTitleAt(const std::vector<RECT>& titles, POINT pt, int current) {
    for (std::size_t i = 0; i < titles.size(); ++i)
        if (int(i) != current && PtInRect(&titles[i], pt)) return int(i);
    return -1;
}

} // namespace md
