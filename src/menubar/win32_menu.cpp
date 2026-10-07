#include "win32_menu.h"

namespace md {
namespace {

constexpr UINT kInitTimeoutMs = 200;
constexpr ULONGLONG kInitBudgetMs = 500;   // pour un menu et tous ses sous-menus

bool send(HWND owner, UINT msg, WPARAM wp, LPARAM lp) {
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(owner, msg, wp, lp, SMTO_ABORTIFHUNG | SMTO_BLOCK, kInitTimeoutMs, &result) != 0;
}

std::vector<RawMenuItem> read(HMENU menu, int depth, bool withChildren) {
    std::vector<RawMenuItem> out;
    const int count = menu ? GetMenuItemCount(menu) : -1;
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info{sizeof(info)};
        info.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_SUBMENU | MIIM_STRING;
        if (!GetMenuItemInfoW(menu, UINT(i), TRUE, &info)) continue;
        const MENUITEMINFOW head = info;   // la lecture du texte ci-dessous réécrit info
        RawMenuItem it;
        it.position = i;
        if (head.fType & MFT_SEPARATOR) {
            if (!out.empty() && !out.back().separator) {   // jamais deux séparateurs de suite ni en tête
                it.separator = true;
                out.push_back(std::move(it));
            }
            continue;
        }
        if (head.fType & (MFT_OWNERDRAW | MFT_BITMAP)) continue;   // pas de texte lisible
        std::wstring raw(info.cch, L'\0');
        if (info.cch > 0) {
            info.fMask = MIIM_STRING;
            info.dwTypeData = raw.data();
            ++info.cch;
            if (!GetMenuItemInfoW(menu, UINT(i), TRUE, &info)) continue;
            raw.resize(info.cch);
        }
        MenuLabel label = parseMenuLabel(raw);
        if (label.text.empty()) continue;
        it.text = std::move(label.text);
        it.shortcut = std::move(label.shortcut);
        it.id = head.wID;
        it.enabled = !(head.fState & (MFS_DISABLED | MFS_GRAYED));
        it.checked = (head.fState & MFS_CHECKED) != 0;
        it.popup = head.hSubMenu != nullptr;
        if (it.popup && withChildren && depth > 0) it.children = read(head.hSubMenu, depth - 1, true);
        out.push_back(std::move(it));
    }
    if (!out.empty() && out.back().separator) out.pop_back();
    return out;
}

void initPopups(HWND owner, HMENU menu, int position, int depth, ULONGLONG start, bool& ok) {
    if (!ok || !menu) return;
    if (GetTickCount64() - start > kInitBudgetMs) {
        ok = false;
        return;
    }
    if (!send(owner, WM_INITMENUPOPUP, reinterpret_cast<WPARAM>(menu), MAKELPARAM(position, FALSE))) {
        ok = false;
        return;
    }
    if (depth <= 0) return;
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count && ok; ++i)
        if (HMENU sub = GetSubMenu(menu, i)) initPopups(owner, sub, i, depth - 1, start, ok);
}

} // namespace

MenuLabel parseMenuLabel(std::wstring_view raw) {
    MenuLabel out;
    const std::size_t tab = raw.find(L'\t');
    const std::wstring_view text = raw.substr(0, tab);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'&') {
            if (i + 1 < text.size() && text[i + 1] == L'&') {
                out.text += L'&';
                ++i;
            }
            continue;
        }
        out.text += text[i];
    }
    if (tab != std::wstring_view::npos) out.shortcut = raw.substr(tab + 1);
    while (!out.shortcut.empty() && iswspace(out.shortcut.back())) out.shortcut.pop_back();
    return out;
}

std::vector<RawMenuItem> readWin32Menu(HMENU menu, int maxDepth) { return read(menu, maxDepth, true); }

std::vector<RawMenuItem> win32MenuTitles(HMENU bar) { return read(bar, 0, false); }

bool refreshWin32Popup(HWND owner, HMENU bar, int position) {
    const ULONGLONG start = GetTickCount64();
    if (!owner || !bar || !send(owner, WM_INITMENU, reinterpret_cast<WPARAM>(bar), 0)) return false;
    bool ok = true;
    initPopups(owner, GetSubMenu(bar, position), position, 3, start, ok);
    return ok;
}

} // namespace md
