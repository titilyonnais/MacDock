// Contenu des menus contextuels du Dock (spec 4.5).
#include <algorithm>

#include "minitest.h"
#include "../src/app/dock_menus.h"

namespace {
const md::MenuItem* find(const std::vector<md::MenuItem>& items, int id) {
    for (auto& it : items) {
        if (it.id == id) return &it;
        if (auto* sub = find(it.submenu, id)) return sub;
    }
    return nullptr;
}
md::MenuContext appContext(bool running, bool pinned) {
    md::MenuContext c;
    c.item.kind = md::ItemKind::App;
    c.item.key = L"app:c:\\a.exe";
    c.item.appId = L"c:\\a.exe";
    c.item.name = L"A";
    c.item.running = running;
    c.item.pinned = pinned;
    c.exePath = L"C:\\a.exe";
    if (running) c.windows = {{11, L"Document 1"}, {12, L"Document 2"}};
    return c;
}
} // namespace

TEST_CASE(menu_app_running_has_quit_and_windows) {
    auto m = md::buildDockMenu(appContext(true, true));
    REQUIRE(find(m.items, md::kCmdWindowBase + 0) != nullptr);
    CHECK(find(m.items, md::kCmdWindowBase + 0)->text == L"Document 1");
    CHECK(find(m.items, md::kCmdWindowBase + 1) != nullptr);
    CHECK(find(m.items, md::kCmdQuit) != nullptr);
    CHECK(find(m.items, md::kCmdHide) != nullptr);
    CHECK(find(m.items, md::kCmdShowAll) != nullptr);
    CHECK(find(m.items, md::kCmdOpen) == nullptr);
    const md::MenuItem* keep = find(m.items, md::kCmdKeep);
    REQUIRE(keep != nullptr);
    CHECK(keep->checked);   // « Garder dans le Dock » coché pour une app épinglée
}

TEST_CASE(menu_app_closed_has_open) {
    auto m = md::buildDockMenu(appContext(false, true));
    CHECK(find(m.items, md::kCmdOpen) != nullptr);
    CHECK(find(m.items, md::kCmdQuit) == nullptr);
    CHECK(find(m.items, md::kCmdHide) == nullptr);
    CHECK(find(m.items, md::kCmdWindowBase) == nullptr);
}

TEST_CASE(menu_app_login_needs_exe) {
    auto c = appContext(true, false);
    c.openAtLogin = true;
    auto m = md::buildDockMenu(c);
    REQUIRE(find(m.items, md::kCmdLogin) != nullptr);
    CHECK(find(m.items, md::kCmdLogin)->checked);
    CHECK(!find(m.items, md::kCmdKeep)->checked);
    c.exePath.clear();   // app empaquetée (AUMID) : pas d'ouverture à la connexion en v1
    CHECK(!find(md::buildDockMenu(c).items, md::kCmdLogin)->enabled);
}

TEST_CASE(menu_separator_toggles_settings) {
    md::MenuContext c;
    c.item.kind = md::ItemKind::Separator;
    c.settings.magnification = true;
    c.settings.autohide = false;
    auto m = md::buildDockMenu(c);
    REQUIRE(find(m.items, md::kCmdMagnify) != nullptr);
    CHECK(find(m.items, md::kCmdMagnify)->text == L"Désactiver l'agrandissement");
    CHECK(find(m.items, md::kCmdAutohide)->text == L"Activer le masquage");
    CHECK(find(m.items, md::kCmdPosBottom)->checked);
    CHECK(find(m.items, md::kCmdPosLeft)->enabled);
    CHECK(find(m.items, md::kCmdPosRight)->enabled);
    c.settings.position = md::DockPosition::Left;
    auto left = md::buildDockMenu(c);
    CHECK(find(left.items, md::kCmdPosLeft)->checked);
    CHECK(!find(left.items, md::kCmdPosBottom)->checked);
    CHECK(find(m.items, md::kCmdSettings) != nullptr);
}

TEST_CASE(menu_trash_empty_disabled_when_empty) {
    md::MenuContext c;
    c.item.kind = md::ItemKind::Trash;
    c.trashFull = false;
    CHECK(!find(md::buildDockMenu(c).items, md::kCmdTrashEmpty)->enabled);
    c.trashFull = true;
    CHECK(find(md::buildDockMenu(c).items, md::kCmdTrashEmpty)->enabled);
    CHECK(find(md::buildDockMenu(c).items, md::kCmdTrashOpen) != nullptr);
}

TEST_CASE(menu_every_menu_can_quit_dock) {
    for (auto kind : {md::ItemKind::App, md::ItemKind::Separator, md::ItemKind::Trash, md::ItemKind::Stack,
                      md::ItemKind::AppsButton, md::ItemKind::MinimizedWindow}) {
        md::MenuContext c;
        c.item.kind = kind;
        CHECK(find(md::buildDockMenu(c).items, md::kCmdQuitDock) != nullptr);
    }
}

TEST_CASE(menu_window_titles_are_truncated) {
    auto c = appContext(true, true);
    c.windows = {{1, std::wstring(80, L'x')}};
    auto m = md::buildDockMenu(c);
    CHECK(find(m.items, md::kCmdWindowBase)->text.size() <= 41u);   // 40 caractères + « … »
}

TEST_CASE(menu_packaged_app_login_and_reveal_disabled) {
    // App du Store : son exe (sous WindowsApps) ne se lance pas hors de son paquet et change à chaque mise à jour.
    auto c = appContext(true, true);
    c.exePath = L"C:\\Program Files\\WindowsApps\\Microsoft.WindowsNotepad_11.2508.4.0_x64__8wekyb3d8bbwe\\Notepad\\Notepad.exe";
    auto m = md::buildDockMenu(c);
    REQUIRE(find(m.items, md::kCmdLogin) != nullptr);
    CHECK(!find(m.items, md::kCmdLogin)->enabled);
    CHECK(!find(m.items, md::kCmdReveal)->enabled);

    auto s = appContext(false, true);
    s.item.launch = L"shell:AppsFolder\\Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    auto ms = md::buildDockMenu(s);
    CHECK(!find(ms.items, md::kCmdLogin)->enabled);
    CHECK(md::isPackagedApp(L"", L"shell:AppsFolder\\X!App"));
    CHECK(!md::isPackagedApp(L"C:\\Tools\\x.exe", L"C:\\Tools\\x.lnk"));
}

TEST_CASE(dock_menus_stack_full) {
    md::MenuContext c;
    c.item.kind = md::ItemKind::Stack;
    c.item.key = L"stack:C:\\D";
    c.stackSort = md::StackSort::Name;
    c.stackView = md::StackView::Grid;
    auto m = md::buildDockMenu(c);
    for (int id : {md::kCmdSortDateAdded, md::kCmdSortName, md::kCmdSortModified, md::kCmdSortKind, md::kCmdViewAuto,
                   md::kCmdViewFan, md::kCmdViewGrid, md::kCmdReveal, md::kCmdRemove})
        REQUIRE(find(m.items, id) != nullptr);
    CHECK(find(m.items, md::kCmdSortName)->checked);
    CHECK(!find(m.items, md::kCmdSortDateAdded)->checked);
    CHECK(find(m.items, md::kCmdViewGrid)->checked);
    CHECK(!find(m.items, md::kCmdViewAuto)->checked);
    // Les choix de tri et de présentation sont dans des sous-menus, pas à plat.
    CHECK(std::none_of(m.items.begin(), m.items.end(), [](const md::MenuItem& it) { return it.id == md::kCmdSortName; }));
}

TEST_CASE(dock_menus_login_enabled_for_aumid) {
    auto c = appContext(true, true);
    c.exePath = L"C:\\Program Files\\WindowsApps\\Microsoft.WindowsCalculator_11.0_x64__8wekyb3d8bbwe\\Calc.exe";
    c.aumid = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    auto m = md::buildDockMenu(c);
    REQUIRE(find(m.items, md::kCmdLogin) != nullptr);
    CHECK(find(m.items, md::kCmdLogin)->enabled);
    CHECK(!find(m.items, md::kCmdLogin)->checked);
    CHECK(!find(m.items, md::kCmdReveal)->enabled);   // l'exe empaqueté ne s'ouvre pas dans l'Explorateur
    c.openAtLogin = true;
    CHECK(find(md::buildDockMenu(c).items, md::kCmdLogin)->checked);
}

TEST_CASE(dock_menus_stack_display_checked) {
    md::MenuContext c;
    c.item.kind = md::ItemKind::Stack;
    c.item.key = L"stack:C:\\D";
    c.stackDisplay = md::StackDisplay::Folder;
    auto m = md::buildDockMenu(c);
    REQUIRE(find(m.items, md::kCmdDisplayStack) != nullptr);
    REQUIRE(find(m.items, md::kCmdDisplayFolder) != nullptr);
    CHECK(find(m.items, md::kCmdDisplayFolder)->checked);
    CHECK(!find(m.items, md::kCmdDisplayStack)->checked);
    CHECK(m.items[0].text == L"Trier par");
    CHECK(m.items[1].text == L"Afficher comme");
}
