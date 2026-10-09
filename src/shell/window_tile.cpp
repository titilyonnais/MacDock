#include "window_tile.h"

#include <dwmapi.h>

#include <algorithm>
#include <map>

namespace md {

namespace {
struct Named {
    TileAction action;
    const wchar_t* name;
};
constexpr Named kNames[] = {{TileAction::Left, L"left"},          {TileAction::Right, L"right"},
                            {TileAction::Top, L"top"},            {TileAction::Bottom, L"bottom"},
                            {TileAction::TopLeft, L"top-left"},   {TileAction::TopRight, L"top-right"},
                            {TileAction::BottomLeft, L"bottom-left"}, {TileAction::BottomRight, L"bottom-right"},
                            {TileAction::Fill, L"fill"},          {TileAction::Center, L"center"},
                            {TileAction::Previous, L"previous"}};

// Cadres gardés par fenêtre, sur le fil de la barre des menus : celui d'avant le premier rangement, et le dernier
// rangement (une fenêtre qui n'a pas bougé depuis garde son cadre d'avant).
std::map<HWND, RECT>& before() {
    static std::map<HWND, RECT> m;
    return m;
}
std::map<HWND, RECT>& placed() {
    static std::map<HWND, RECT> m;
    return m;
}
} // namespace

std::optional<TileAction> parseTileAction(std::wstring_view name) {
    for (const Named& n : kNames)
        if (name == n.name) return n.action;
    return std::nullopt;
}

std::wstring tileActionName(TileAction a) {
    for (const Named& n : kNames)
        if (n.action == a) return n.name;
    return L"";
}

RECT tileRect(TileAction a, const RECT& work, const RECT& current, int margin) {
    const RECT in{work.left + margin, work.top + margin, work.right - margin, work.bottom - margin};
    const LONG w = in.right - in.left, h = in.bottom - in.top;
    const LONG halfW = (w - margin) / 2, halfH = (h - margin) / 2;
    const LONG leftR = in.left + halfW, rightL = in.right - halfW, topB = in.top + halfH, bottomT = in.bottom - halfH;
    switch (a) {
        case TileAction::Left: return {in.left, in.top, leftR, in.bottom};
        case TileAction::Right: return {rightL, in.top, in.right, in.bottom};
        case TileAction::Top: return {in.left, in.top, in.right, topB};
        case TileAction::Bottom: return {in.left, bottomT, in.right, in.bottom};
        case TileAction::TopLeft: return {in.left, in.top, leftR, topB};
        case TileAction::TopRight: return {rightL, in.top, in.right, topB};
        case TileAction::BottomLeft: return {in.left, bottomT, leftR, in.bottom};
        case TileAction::BottomRight: return {rightL, bottomT, in.right, in.bottom};
        case TileAction::Fill: return in;
        case TileAction::Center: {
            const LONG cw = std::min(w, current.right - current.left), ch = std::min(h, current.bottom - current.top);
            const LONG x = in.left + (w - cw) / 2, y = in.top + (h - ch) / 2;
            return {x, y, x + cw, y + ch};
        }
        case TileAction::Previous: return current;
    }
    return current;
}

bool tileWindow(HWND h, TileAction a) {
    if (!IsWindow(h)) return false;
    if (IsIconic(h) || IsZoomed(h)) {   // restaurée d'abord : ses bordures invisibles ne sont pas celles d'une agrandie
        ShowWindowAsync(h, SW_RESTORE);
        for (int i = 0; i < 30 && (IsIconic(h) || IsZoomed(h)); ++i) Sleep(10);
    }
    RECT win{}, vis{};
    if (!GetWindowRect(h, &win)) return false;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &vis, sizeof vis)) || IsRectEmpty(&vis)) vis = win;
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    RECT target{};
    if (a == TileAction::Previous) {
        const auto it = before().find(h);
        if (it == before().end()) return false;
        target = it->second;
        before().erase(it);
        placed().erase(h);
    } else {
        const UINT dpi = GetDpiForWindow(h);
        target = tileRect(a, mi.rcWork, vis, MulDiv(8, dpi ? int(dpi) : 96, 96));
        // Pas bougée depuis notre dernier rangement : le cadre d'avant reste celui du tout premier.
        const auto last = placed().find(h);
        if (before().count(h) == 0 || last == placed().end() || !EqualRect(&last->second, &vis)) before()[h] = vis;
        placed()[h] = target;
    }
    // Rectangle de Windows = cadre visible voulu + les bordures invisibles actuelles.
    const RECT r{target.left - (vis.left - win.left), target.top - (vis.top - win.top), target.right + (win.right - vis.right),
                 target.bottom + (win.bottom - vis.bottom)};
    return SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top,
                        SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS) != FALSE;
}

} // namespace md
