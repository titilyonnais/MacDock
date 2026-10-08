#include "menu_model.h"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <vector>

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
        case MenuRow::Calendar: return kMenuCalendarHeight;
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

namespace {
std::wstring lowered(std::wstring_view s) {   // Unicode (« ÉCHAP » → « échap »), espaces retirés
    std::wstring l;
    for (wchar_t c : s)
        if (c != L' ') l += c;
    if (!l.empty())
        CharLowerBuffW(l.data(), DWORD(l.size()));
    return l;
}
} // namespace

std::wstring macShortcutLabel(std::wstring_view shortcut) {
    if (shortcut.empty()) return {};
    // Accord (« Ctrl+K, Ctrl+C ») : laissé tel quel plutôt que réduit à sa dernière touche.
    if (shortcut.find(L", ") != std::wstring_view::npos) return std::wstring(shortcut);
    // Morceaux séparés par « + » ; un « + » final (« Ctrl++ ») est la touche plus elle-même.
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (std::size_t i = 0; i < shortcut.size(); ++i) {
        if (shortcut[i] == L' ') continue;
        if (shortcut[i] == L'+' && !cur.empty()) {
            parts.push_back(cur);
            cur.clear();
        } else {
            cur += shortcut[i];
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    bool ctrl = false, alt = false, shift = false, cmd = false, win = false;
    std::wstring key;
    for (const auto& part : parts) {
        const std::wstring p = lowered(part);
        if (p == L"ctrl" || p == L"ctl" || p == L"control" || p == L"contrôle") ctrl = true;
        else if (p == L"alt" || p == L"option") alt = true;
        else if (p == L"maj" || p == L"shift") shift = true;
        else if (p == L"cmd" || p == L"commande") cmd = true;
        else if (p == L"win" || p == L"windows") win = true;
        else key = part;
    }
    static const std::pair<const wchar_t*, const wchar_t*> kKeys[] = {
        {L"suppr", L"⌦"}, {L"del", L"⌦"}, {L"delete", L"⌦"}, {L"retourarrière", L"⌫"}, {L"backspace", L"⌫"},
        {L"entrée", L"↩"}, {L"enter", L"↩"}, {L"retour", L"↩"}, {L"échap", L"⎋"}, {L"esc", L"⎋"}, {L"tab", L"⇥"},
        {L"origine", L"↖"}, {L"home", L"↖"}, {L"fin", L"↘"}, {L"end", L"↘"}, {L"pg.préc", L"⇞"}, {L"pgup", L"⇞"},
        {L"pg.suiv", L"⇟"}, {L"pgdn", L"⇟"}, {L"haut", L"↑"}, {L"bas", L"↓"}, {L"gauche", L"←"}, {L"droite", L"→"},
        {L"plus", L"+"}, {L"moins", L"−"}, {L"espace", L"Espace"}, {L"space", L"Espace"}};
    const std::wstring lk = lowered(key);
    std::wstring shown = key;
    for (const auto& [name, glyph] : kKeys)
        if (lk == name) shown = glyph;
    if (shown == key && key.size() == 1) shown = std::wstring(1, wchar_t(std::towupper(key[0])));
    std::wstring out;
    if (win) out += L"⊞";
    if (ctrl) out += L"⌃";
    if (alt) out += L"⌥";
    if (shift) out += L"⇧";
    if (cmd) out += L"⌘";
    return out + shown;
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

namespace {
int daysIn(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[(month - 1 + 12) % 12];
}
int dayOfWeek(int y, int m, int d) {   // 0 = dimanche (méthode de Sakamoto)
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
} // namespace

std::vector<CalendarCell> calendarCells(int year, int month, int today) {
    std::vector<CalendarCell> out(42);
    if (month < 1 || month > 12) return out;
    const int lead = (dayOfWeek(year, month, 1) + 6) % 7;   // lundi en premier
    const int days = daysIn(year, month), before = daysIn(month == 1 ? year - 1 : year, month == 1 ? 12 : month - 1);
    for (int i = 0; i < 42; ++i) {
        CalendarCell& c = out[std::size_t(i)];
        const int d = i - lead + 1;
        c.inMonth = d >= 1 && d <= days;
        c.day = d < 1 ? before + d : d > days ? d - days : d;
        c.today = c.inMonth && d == today;
    }
    return out;
}

} // namespace md
