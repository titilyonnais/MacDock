#include "dock_menus.h"

#include "../core/strings.h"

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
    // App empaquetée : pas d'exe utilisable ; l'ouverture à la connexion passe par un raccourci vers l'AUMID.
    const bool packaged = isPackagedApp(c.exePath, item.launch);
    const bool exeUsable = !c.exePath.empty() && !packaged;
    const bool loginUsable = exeUsable || (packaged && !c.aumid.empty());
    options.submenu = {entry(kCmdKeep, L"Garder dans le Dock", true, item.pinned),
                       entry(kCmdLogin, L"Ouvrir à la connexion", loginUsable, c.openAtLogin),
                       entry(kCmdReveal, L"Afficher dans l'Explorateur", exeUsable)};
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
    pos.submenu = {entry(kCmdPosLeft, L"Gauche", true, s.position == DockPosition::Left),
                   entry(kCmdPosBottom, L"En bas", true, s.position == DockPosition::Bottom),
                   entry(kCmdPosRight, L"Droite", true, s.position == DockPosition::Right)};
    out.push_back(pos);
    out.push_back({});
    out.push_back(entry(kCmdSettings, L"Réglages du Dock…"));
}

void stackMenu(const MenuContext& c, std::vector<MenuItem>& out) {
    MenuItem sort{-1, L"Trier par"};
    sort.submenu = {entry(kCmdSortName, L"Nom", true, c.stackSort == StackSort::Name),
                    entry(kCmdSortDateAdded, L"Date d'ajout", true, c.stackSort == StackSort::DateAdded),
                    entry(kCmdSortModified, L"Date de modification", true, c.stackSort == StackSort::Modified),
                    entry(kCmdSortKind, L"Type", true, c.stackSort == StackSort::Kind)};
    out.push_back(sort);
    MenuItem display{-1, L"Afficher comme"};
    display.submenu = {entry(kCmdDisplayStack, L"Pile", true, c.stackDisplay == StackDisplay::Stack),
                       entry(kCmdDisplayFolder, L"Dossier", true, c.stackDisplay == StackDisplay::Folder)};
    out.push_back(display);
    MenuItem view{-1, L"Présenter le contenu comme"};
    view.submenu = {entry(kCmdViewFan, L"Éventail", true, c.stackView == StackView::Fan),
                    entry(kCmdViewGrid, L"Grille", true, c.stackView == StackView::Grid),
                    entry(kCmdViewList, L"Liste", true, c.stackView == StackView::List),
                    entry(kCmdViewAuto, L"Automatiquement", true, c.stackView == StackView::Auto)};
    out.push_back(view);
    out.push_back({});
    out.push_back(entry(kCmdReveal, L"Ouvrir dans l'Explorateur"));
    out.push_back({});
    out.push_back(entry(kCmdRemove, L"Retirer du Dock"));
}

} // namespace

bool isPackagedApp(const std::wstring& exePath, const std::wstring& launch) {
    if (toLower(launch).starts_with(L"shell:appsfolder\\")) return true;
    return toLower(exePath).find(L"\\windowsapps\\") != std::wstring::npos;
}

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
        case ItemKind::Stack: stackMenu(c, out); break;
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
