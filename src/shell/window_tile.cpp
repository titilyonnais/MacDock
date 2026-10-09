#include "window_tile.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <map>

#pragma comment(lib, "shcore.lib")

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

// Cadres gardés par fenêtre, sur le fil de la barre des menus : celui d'avant le premier rangement, et le dernier cadre
// obtenu (une fenêtre qui n'a pas bougé depuis garde son cadre d'avant). Le processus va avec : un HWND repris par une
// autre fenêtre ne reçoit pas le cadre de la précédente.
struct Kept {
    RECT frame{};
    DWORD pid = 0;
};
std::map<HWND, Kept>& before() {
    static std::map<HWND, Kept> m;
    return m;
}
std::map<HWND, Kept>& placed() {
    static std::map<HWND, Kept> m;
    return m;
}
DWORD processOf(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid;
}
// Fenêtres fermées, ou HWND repris par un autre processus : oubliées.
void purge() {
    for (auto* m : {&before(), &placed()})
        for (auto it = m->begin(); it != m->end();) it = !IsWindow(it->first) || processOf(it->first) != it->second.pid ? m->erase(it) : std::next(it);
}
RECT visibleFrame(HWND h, const RECT& window) {
    RECT v{};
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &v, sizeof v)) || IsRectEmpty(&v)) return window;
    return v;
}
bool resizes(TileAction a) { return a != TileAction::Center && a != TileAction::Previous; }

struct NamedArrangement {
    Arrangement arrangement;
    const wchar_t* name;
};
constexpr NamedArrangement kArrangements[] = {{Arrangement::LeftRight, L"left-right"},
                                              {Arrangement::RightLeft, L"right-left"},
                                              {Arrangement::TopBottom, L"top-bottom"},
                                              {Arrangement::BottomTop, L"bottom-top"},
                                              {Arrangement::Quarters, L"quarters"}};

bool isCloaked(HWND h) {   // sur un autre bureau virtuel, ou cachée par DWM
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked;
}

// Fenêtre d'app qu'on peut organiser : visible sur ce bureau, à barre de titre et redimensionnable, sans propriétaire
// (dialogues, palettes) ni style outil.
bool arrangeEligible(HWND h) {
    if (!IsWindowVisible(h) || IsIconic(h) || isCloaked(h)) return false;
    const LONG_PTR style = GetWindowLongPtrW(h, GWL_STYLE), ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if ((style & WS_CAPTION) != WS_CAPTION || !(style & WS_THICKFRAME) || (style & WS_CHILD)) return false;
    if ((ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) || GetWindow(h, GW_OWNER)) return false;
    RECT r{};
    return GetWindowRect(h, &r) && !IsRectEmpty(&r);
}

BOOL CALLBACK collectCandidate(HWND h, LPARAM lp) {   // EnumWindows : de l'avant vers l'arrière
    reinterpret_cast<std::vector<ArrangeCandidate>*>(lp)->push_back(
        {h, arrangeEligible(h), MonitorFromWindow(h, MONITOR_DEFAULTTONULL), (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0});
    return TRUE;
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

std::optional<Arrangement> parseArrangement(std::wstring_view name) {
    for (const NamedArrangement& n : kArrangements)
        if (name == n.name) return n.arrangement;
    return std::nullopt;
}

std::wstring arrangementName(Arrangement a) {
    for (const NamedArrangement& n : kArrangements)
        if (n.arrangement == a) return n.name;
    return L"";
}

std::vector<TileAction> arrangementSlots(Arrangement a) {
    switch (a) {
        case Arrangement::LeftRight: return {TileAction::Left, TileAction::Right};
        case Arrangement::RightLeft: return {TileAction::Right, TileAction::Left};
        case Arrangement::TopBottom: return {TileAction::Top, TileAction::Bottom};
        case Arrangement::BottomTop: return {TileAction::Bottom, TileAction::Top};
        case Arrangement::Quarters:
            return {TileAction::TopLeft, TileAction::TopRight, TileAction::BottomLeft, TileAction::BottomRight};
    }
    return {};
}

std::vector<HWND> arrangeCandidates(const std::vector<ArrangeCandidate>& zOrder, HWND first, HMONITOR monitor) {
    bool firstTopmost = false;
    for (const ArrangeCandidate& c : zOrder)
        if (c.window == first) firstTopmost = c.topmost;
    std::vector<HWND> out{first};
    for (const bool band : {firstTopmost, !firstTopmost})   // la bande de la fenêtre choisie d'abord
        for (const ArrangeCandidate& c : zOrder)
            if (c.window != first && c.eligible && c.monitor == monitor && c.topmost == band) out.push_back(c.window);
    return out;
}

RECT anchorTile(TileAction a, const RECT& target, const RECT& got) {
    const LONG w = got.right - got.left, h = got.bottom - got.top;
    const bool right = a == TileAction::Right || a == TileAction::TopRight || a == TileAction::BottomRight;
    const bool bottom = a == TileAction::Bottom || a == TileAction::BottomLeft || a == TileAction::BottomRight;
    const LONG x = right ? target.right - w : target.left, y = bottom ? target.bottom - h : target.top;
    return {x, y, x + w, y + h};
}

bool tileWindow(HWND h, TileAction a) {
    purge();
    if (!IsWindow(h) || IsIconic(h) || IsHungAppWindow(h)) return false;   // réduite : macOS grise ces entrées
    if (resizes(a) && !(GetWindowLongPtrW(h, GWL_STYLE) & WS_THICKFRAME)) return false;   // taille fixe : Centrer seul
    if (IsZoomed(h)) {   // synchrone : son cadre et ses bordures sont ceux d'une fenêtre restaurée ensuite
        ShowWindow(h, SW_RESTORE);
        if (IsZoomed(h) || IsIconic(h)) return false;
    }
    RECT win{};
    if (!GetWindowRect(h, &win)) return false;
    const RECT vis = visibleFrame(h, win);
    MONITORINFO mi{sizeof mi};
    const HMONITOR mon = MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    if (!GetMonitorInfoW(mon, &mi)) return false;
    const DWORD pid = processOf(h);
    RECT target{};
    if (a == TileAction::Previous) {
        const auto it = before().find(h);
        if (it == before().end()) return false;
        target = it->second.frame;
    } else {
        UINT dpi = 96, dpiY = 96;   // celui de l'écran : les coordonnées sont physiques
        if (FAILED(GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi, &dpiY))) dpi = 96;
        target = tileRect(a, mi.rcWork, vis, MulDiv(8, int(dpi), 96));
    }
    // Rectangle de Windows = cadre visible + bordures invisibles (celles de maintenant).
    const auto place = [&](const RECT& t, UINT flags) {
        const RECT r{t.left - (vis.left - win.left), t.top - (vis.top - win.top), t.right + (win.right - vis.right),
                     t.bottom + (win.bottom - vis.bottom)};
        return SetWindowPos(h, nullptr, r.left, r.top, r.right - r.left, r.bottom - r.top, flags) != FALSE;
    };
    if (!place(target, SWP_NOZORDER | SWP_NOACTIVATE)) return false;   // refus (UIPI) : rien de gardé
    RECT got = visibleFrame(h, win);
    if (RECT now{}; GetWindowRect(h, &now)) got = visibleFrame(h, now);
    if (resizes(a) && !EqualRect(&got, &target)) {   // taille minimale de l'app : recalée contre le bord visé
        const RECT anchored = anchorTile(a, target, got);
        RECT now{};
        GetWindowRect(h, &now);
        const RECT v = visibleFrame(h, now);
        SetWindowPos(h, nullptr, now.left + (anchored.left - v.left), now.top + (anchored.top - v.top), 0, 0,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
        if (GetWindowRect(h, &now)) got = visibleFrame(h, now);
    }
    if (a == TileAction::Previous) {
        before().erase(h);
        placed().erase(h);
    } else {
        const auto last = placed().find(h);
        if (before().count(h) == 0 || last == placed().end() || !EqualRect(&last->second.frame, &vis)) before()[h] = {vis, pid};
        placed()[h] = {got, pid};
    }
    return true;
}

bool arrangeWindows(HWND first, Arrangement a) {
    if (!IsWindow(first)) return false;
    std::vector<ArrangeCandidate> z;
    EnumWindows(collectCandidate, reinterpret_cast<LPARAM>(&z));
    const std::vector<HWND> order = arrangeCandidates(z, first, MonitorFromWindow(first, MONITOR_DEFAULTTONEAREST));
    const std::vector<TileAction> slots = arrangementSlots(a);
    if (slots.empty() || !tileWindow(first, slots[0])) return false;
    HWND above = first;
    std::size_t next = 1;
    for (std::size_t s = 1; s < slots.size(); ++s)
        for (; next < order.size(); ++next) {
            const HWND h = order[next];
            DWORD_PTR answer = 0;   // occupée (pas de réponse en 100 ms) : sautée, la barre ne se fige pas
            if (!SendMessageTimeoutW(h, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &answer)) continue;
            if (!tileWindow(h, slots[s])) continue;
            // Juste sous la précédente (une fenêtre qui la recouvrait passe derrière), sans changer de bande.
            if (((GetWindowLongPtrW(h, GWL_EXSTYLE) ^ GetWindowLongPtrW(above, GWL_EXSTYLE)) & WS_EX_TOPMOST) == 0)
                SetWindowPos(h, above, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
            above = h;
            ++next;
            break;
        }
    return true;
}

} // namespace md
