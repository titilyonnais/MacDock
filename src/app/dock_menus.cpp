#include "dock_menus.h"

namespace md {

namespace {

constexpr std::size_t kMaxTitle = 40;

std::wstring shorten(const std::wstring& s) {
    if (s.size() <= kMaxTitle) return s;
    return s.substr(0, kMaxTitle) + L"…";
}

MenuItem entry(int id, const std::wstring& text, bool enabled = true, bool checked = false) {
    MenuItem it{id, text};
    it.enabled = enabled;
    it.checked = checked;
    return it;
}

void appMenu(const MenuContext& c, std::vector<MenuItem>& out) {
    const DockItem& item = c.item;
    // Fenêtres ouvertes en tête, comme sur macOS.
    for (std::size_t i = 0; i < c.windows.size(); ++i) {
        std::wstring title = c.windows[i].second.empty() ? item.name : c.windows[i].second;
        out.push_back(entry(kCmdWindowBase + int(i), shorten(title)));
    }
    if (!c.windows.empty()) out.push_back({});

    MenuItem options{-1, L"Options"};   // identifiant non nul : entrée à sous-menu
    options.submenu = {entry(kCmdKeep, L"Garder dans le Dock", true, item.pinned),
                       entry(kCmdLogin, L"Ouvrir à la connexion", !c.exePath.empty(), c.openAtLogin),
                       entry(kCmdReveal, L"Afficher dans l'Explorateur", !c.exePath.empty())};
    out.push_back(options);
    out.push_back({});
    if (item.running) {
        out.push_back(entry(kCmdShowAll, L"Afficher toutes les fenêtres"));
        out.push_back(entry(kCmdHide, L"Masquer"));
        out.push_back(entry(kCmdQuit, L"Quitter"));
    } else {
        out.push_back(entry(kCmdOpen, L"Ouvrir"));
    }
}

void separatorMenu(const MenuContext& c, std::vector<MenuItem>& out) {
    const Settings& s = c.settings;
    out.push_back(entry(kCmdAutohide, s.autohide ? L"Désactiver le masquage" : L"Activer le masquage"));
    out.push_back(entry(kCmdMagnify, s.magnification ? L"Désactiver l'agrandissement" : L"Activer l'agrandissement"));
    MenuItem pos{-1, L"Position à l'écran"};
    // Gauche et Droite arrivent avec le plan 4 (la mise en page n'a encore qu'un axe).
    pos.submenu = {entry(kCmdPosLeft, L"Gauche", false, s.position == DockPosition::Left),
                   entry(kCmdPosBottom, L"En bas", true, s.position == DockPosition::Bottom),
                   entry(kCmdPosRight, L"Droite", false, s.position == DockPosition::Right)};
    out.push_back(pos);
    out.push_back({});
    out.push_back(entry(kCmdSettings, L"Réglages du Dock…"));
}

} // namespace

MenuModel buildDockMenu(const MenuContext& c) {
    MenuModel m;
    auto& out = m.items;
    switch (c.item.kind) {
        case ItemKind::App: appMenu(c, out); break;
        case ItemKind::Separator: separatorMenu(c, out); break;
        case ItemKind::Trash:
            out.push_back(entry(kCmdTrashOpen, L"Ouvrir"));
            out.push_back({});
            out.push_back(entry(kCmdTrashEmpty, L"Vider la Corbeille", c.trashFull));
            break;
        case ItemKind::Stack:
            out.push_back(entry(kCmdReveal, L"Ouvrir dans l'Explorateur"));
            out.push_back({});
            out.push_back(entry(kCmdRemove, L"Retirer du Dock"));
            break;
        case ItemKind::AppsButton: out.push_back(entry(kCmdRemove, L"Retirer du Dock")); break;
        case ItemKind::MinimizedWindow:
            out.push_back(entry(kCmdRestore, L"Restaurer"));
            out.push_back(entry(kCmdCloseWindow, L"Fermer"));
            break;
    }
    out.push_back({});
    out.push_back(entry(kCmdQuitDock, L"Quitter MacDock"));
    return m;
}

} // namespace md
