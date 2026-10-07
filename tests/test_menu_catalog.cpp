// Menus propres aux apps courantes (catalogue) : titres à la manière de leurs menus sur macOS, vrais raccourcis.
#include "minitest.h"

#include <functional>
#include <string>

#include "../src/menubar/menu_catalog.h"
#include "../src/menubar/app_menus.h"
#include "../src/menubar/shortcut.h"

namespace {
const md::BarMenu* menuNamed(const md::BarMenus& b, const std::wstring& title) {
    for (auto& m : b.menus)
        if (m.title == title) return &m;
    return nullptr;
}
const md::MenuItem* itemNamed(const std::vector<md::MenuItem>& items, const std::wstring& text) {
    for (auto& it : items) {
        if (it.text == text) return &it;
        if (auto* sub = itemNamed(it.submenu, text)) return sub;
    }
    return nullptr;
}
md::MenuAction actionOf(const md::BarMenus& b, const md::MenuItem* it) {
    if (!it) return {};
    auto f = b.actions.find(it->id);
    return f == b.actions.end() ? md::MenuAction{} : f->second;
}
md::BarContext appContext(const wchar_t* name, const wchar_t* exe) {
    md::BarContext c;
    c.appName = name;
    c.exe = exe;
    c.userName = L"Camille";
    return c;
}
std::vector<std::wstring> titles(const md::BarMenus& b) {
    std::vector<std::wstring> t;
    for (std::size_t i = 2; i < b.menus.size(); ++i) t.push_back(b.menus[i].title);   // après le logo et l'app
    return t;
}
} // namespace

TEST_CASE(catalog_finds_apps_by_executable_name) {
    CHECK(md::menuCatalogFor(L"brave.exe") != nullptr);
    CHECK(md::menuCatalogFor(L"BRAVE.EXE") == md::menuCatalogFor(L"brave.exe"));   // casse indifférente
    CHECK(md::menuCatalogFor(L"chrome.exe") == md::menuCatalogFor(L"brave.exe"));   // même famille Chromium
    CHECK(md::menuCatalogFor(L"discord.exe") != nullptr);
    CHECK(md::menuCatalogFor(L"WindowsTerminal.exe") != nullptr);
    CHECK(md::menuCatalogFor(L"inconnu.exe") == nullptr);
    CHECK(md::menuCatalogFor(L"") == nullptr);
}

TEST_CASE(catalog_browser_menus_follow_chrome_on_macos) {
    auto b = md::buildBarMenus(appContext(L"Brave", L"brave.exe"));
    const std::vector<std::wstring> want{L"Fichier", L"Édition", L"Présentation", L"Historique", L"Favoris", L"Onglet",
                                         L"Fenêtre", L"Aide"};
    CHECK(titles(b) == want);
    auto* priv = itemNamed(menuNamed(b, L"Fichier")->model.items, L"Nouvelle fenêtre de navigation privée");
    REQUIRE(priv != nullptr);
    CHECK(priv->shortcut == L"Ctrl+Maj+N");
    CHECK(actionOf(b, priv).kind == md::ActionKind::Shortcut);
    CHECK(actionOf(b, priv).arg == L"Ctrl+Maj+N");
    // Entrées propres à la fenêtre du navigateur, en tête du menu Fenêtre comme sur macOS.
    CHECK(itemNamed(menuNamed(b, L"Fenêtre")->model.items, L"Téléchargements") != nullptr);
    CHECK(itemNamed(menuNamed(b, L"Fenêtre")->model.items, L"Réduire") != nullptr);
    // Sous-menu Développeur de Présentation.
    auto* dev = itemNamed(menuNamed(b, L"Présentation")->model.items, L"Outils de développement");
    REQUIRE(dev != nullptr);
    CHECK(dev->shortcut == L"Ctrl+Maj+I");
}

TEST_CASE(catalog_terminal_has_shell_menu_like_macos_terminal) {
    auto b = md::buildBarMenus(appContext(L"Terminal", L"windowsterminal.exe"));
    const std::vector<std::wstring> want{L"Shell", L"Édition", L"Présentation", L"Fenêtre", L"Aide"};
    CHECK(titles(b) == want);
    auto* copy = itemNamed(menuNamed(b, L"Édition")->model.items, L"Copier");
    REQUIRE(copy != nullptr);
    CHECK(copy->shortcut == L"Ctrl+Maj+C");   // dans le Terminal, Ctrl+C interrompt
}

TEST_CASE(catalog_app_menu_settings_follow_the_app) {
    // Réglages : raccourci de l'app (Ctrl+,) ou adresse (Steam) ; absent si l'app n'en a pas.
    auto discord = md::buildBarMenus(appContext(L"Discord", L"discord.exe"));
    CHECK(actionOf(discord, itemNamed(discord.menus[1].model.items, L"Réglages…")).arg == L"Ctrl+,");
    auto steam = md::buildBarMenus(appContext(L"Steam", L"steam.exe"));
    auto settings = actionOf(steam, itemNamed(steam.menus[1].model.items, L"Réglages…"));
    CHECK(settings.kind == md::ActionKind::OpenUri);
    CHECK(settings.arg == L"steam://open/settings");
    auto brave = md::buildBarMenus(appContext(L"Brave", L"brave.exe"));
    auto* braveSettings = itemNamed(brave.menus[1].model.items, L"Réglages…");
    CHECK(braveSettings == nullptr || !braveSettings->enabled || actionOf(brave, braveSettings).kind != md::ActionKind::None);
}

TEST_CASE(catalog_every_shortcut_is_understood) {
    // Une faute de frappe dans le catalogue donnerait une entrée muette : chaque raccourci doit être analysable.
    int checked = 0;
    for (const auto& app : md::menuCatalog()) {
        std::function<void(const std::vector<md::CatalogItem>&)> walk = [&](const std::vector<md::CatalogItem>& items) {
            for (const auto& it : items) {
                if (!it.shortcut.empty()) {
                    CHECK(md::parseShortcut(it.shortcut).has_value());
                    if (!md::parseShortcut(it.shortcut)) std::wprintf(L"    raccourci inconnu : %s\n", it.shortcut.c_str());
                    ++checked;
                }
                walk(it.submenu);
            }
        };
        for (const auto& m : app.menus) walk(m.items);
        walk(app.windowExtras);
    }
    CHECK(checked > 100);
}

TEST_CASE(shortcut_understands_navigation_keys) {
    CHECK(md::parseShortcut(L"Alt+Origine").has_value());
    CHECK(md::parseShortcut(L"Ctrl+Fin").has_value());
    CHECK(md::parseShortcut(L"Ctrl+Pg.suiv").has_value());
    CHECK(md::parseShortcut(L"Ctrl+Pg.préc").has_value());
    CHECK(md::parseShortcut(L"Origine")->key == WORD(VK_HOME));
}

TEST_CASE(catalog_unknown_app_keeps_generic_menus) {
    auto b = md::buildBarMenus(appContext(L"Notes", L"notes.exe"));
    const std::vector<std::wstring> want{L"Fichier", L"Édition", L"Présentation", L"Fenêtre", L"Aide"};
    CHECK(titles(b) == want);
}
