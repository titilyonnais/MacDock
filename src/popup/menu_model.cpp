#include "menu_model.h"

#include <algorithm>
#include <cmath>

namespace md {

MenuLayout layoutMenu(const MenuModel& m, double textWidthMax) {
    MenuLayout l;
    double y = kMenuPadding;
    for (auto& it : m.items) {
        l.top.push_back(y);
        y += it.separator() ? kMenuSeparatorHeight : kMenuItemHeight;
    }
    l.height = y + kMenuPadding;
    double text = std::isfinite(textWidthMax) ? std::max(0.0, textWidthMax) : 0.0;
    if (std::any_of(m.items.begin(), m.items.end(), [](const MenuItem& it) { return it.icon != nullptr; }))
        l.iconSpace = kMenuIconSize + kMenuIconGap;
    l.width = std::max(kMenuMinWidth, std::ceil(2 * kMenuPadding + kMenuTextLeft + l.iconSpace + text + kMenuTextRight));
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
        double h = it.separator() ? kMenuSeparatorHeight : kMenuItemHeight;
        if (y >= l.top[i] && y < l.top[i] + h) return it.selectable() ? int(i) : -1;
    }
    return -1;
}

} // namespace md
