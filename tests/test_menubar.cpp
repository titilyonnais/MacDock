// Barre de menus : logique pure (mise en page, couleur, horloge, raccourcis, premier plan, réglages).
#include <windows.h>

#include "minitest.h"
#include "../src/core/json.h"
#include "../src/menubar/bar_color.h"
#include "../src/menubar/bar_layout.h"
#include "../src/menubar/clock_format.h"
#include "../src/menubar/foreground_rules.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/shortcut.h"

#include <functional>
#include <set>
#include <thread>

namespace {
md::BarLayoutInput wideBar(double width) {
    md::BarLayoutInput in;
    in.barWidth = width;
    in.leftWidths = {34, 80, 60, 60, 60};   // logo, nom de l'app, trois menus
    in.rightWidths = {150};                 // horloge
    return in;
}
} // namespace

TEST_CASE(bar_layout_places_left_and_right) {
    auto in = wideBar(1000);
    auto l = md::layoutBar(in);
    REQUIRE(l.leftX.size() == 5);
    CHECK_NEAR(l.leftX[0], 10, 1e-9);
    CHECK_NEAR(l.leftX[1], 44, 1e-9);
    CHECK_NEAR(l.leftX[4], 244, 1e-9);
    CHECK_EQ(l.leftVisible, std::size_t(5));
    REQUIRE(l.rightX.size() == 1);
    CHECK_NEAR(l.rightX[0], 840, 1e-9);   // 1000 - 10 de marge - 150
}

TEST_CASE(bar_layout_hides_menus_before_status) {
    // 400 pt : l'horloge commence à 240 ; un menu doit finir 20 pt avant.
    auto l = md::layoutBar(wideBar(400));
    CHECK_EQ(l.leftVisible, std::size_t(3));   // le 4e menu (184..244) toucherait l'horloge
    // Trop étroit pour tout : le logo et le nom de l'app restent quand même.
    CHECK_EQ(md::layoutBar(wideBar(150)).leftVisible, std::size_t(2));
}

TEST_CASE(bar_hit_test_left_right_none) {
    auto in = wideBar(1000);
    auto l = md::layoutBar(in);
    auto h = md::hitTestBar(l, in, 20);
    CHECK(h.kind == md::BarHit::Kind::Left);
    CHECK_EQ(h.index, std::size_t(0));
    h = md::hitTestBar(l, in, 130);
    CHECK(h.kind == md::BarHit::Kind::Left);
    CHECK_EQ(h.index, std::size_t(2));
    h = md::hitTestBar(l, in, 900);
    CHECK(h.kind == md::BarHit::Kind::Right);
    CHECK_EQ(h.index, std::size_t(0));
    CHECK(md::hitTestBar(l, in, 500).kind == md::BarHit::Kind::None);
    // Une case masquée ne se clique pas.
    auto narrowIn = wideBar(400);
    auto narrow = md::layoutBar(narrowIn);
    CHECK(md::hitTestBar(narrow, narrowIn, 200).kind == md::BarHit::Kind::None);
}

TEST_CASE(bar_color_white_black_luminance) {
    std::vector<std::uint8_t> white(4 * 4 * 2, 255), black(4 * 4 * 2, 0);
    CHECK_NEAR(md::stripLuminance(white.data(), 4, 2, 16), 1.0, 1e-6);
    CHECK_NEAR(md::stripLuminance(black.data(), 4, 2, 16), 0.0, 1e-6);
    CHECK_NEAR(md::srgbToLinear(128.0 / 255.0), 0.2158, 1e-3);
    // Vert pur : 0,7152 (coefficients de la luminance relative).
    std::vector<std::uint8_t> green{0, 255, 0, 255};
    CHECK_NEAR(md::stripLuminance(green.data(), 1, 1, 4), 0.7152, 1e-4);
}

TEST_CASE(bar_color_hysteresis) {
    CHECK(md::chooseDarkText(0.9, false));
    CHECK(!md::chooseDarkText(0.1, true));
    CHECK(md::chooseDarkText(0.40, true));    // entre les seuils : on garde l'état
    CHECK(!md::chooseDarkText(0.40, false));
    CHECK(md::chooseDarkText(0.46, false));
    CHECK(!md::chooseDarkText(0.34, true));
}

TEST_CASE(bar_color_half_matches_srgb) {
    CHECK_NEAR(md::halfToFloat(0x3C00), 1.0, 1e-6);
    CHECK_NEAR(md::halfToFloat(0x3800), 0.5, 1e-6);
    CHECK_NEAR(md::halfToFloat(0), 0.0, 1e-6);
    CHECK_NEAR(md::halfToFloat(0xC000), -2.0, 1e-6);
    // scRGB 0,5 linéaire ≈ sRGB 188.
    std::vector<std::uint16_t> half{0x3800, 0x3800, 0x3800, 0x3C00};
    std::vector<std::uint8_t> srgb{188, 188, 188, 255};
    CHECK_NEAR(md::stripLuminanceHalf(half.data(), 1, 1, 4, 1.0), md::stripLuminance(srgb.data(), 1, 1, 4), 0.01);
    // En HDR, le blanc SDR vaut sdrWhite : il est ramené à 1, et le plus lumineux est borné à 1.
    std::vector<std::uint16_t> bright{0x4100, 0x4100, 0x4100, 0x3C00};   // 2,5
    CHECK_NEAR(md::stripLuminanceHalf(bright.data(), 1, 1, 4, 2.5), 1.0, 1e-6);
    CHECK_NEAR(md::stripLuminanceHalf(bright.data(), 1, 1, 4, 1.0), 1.0, 1e-6);
}

TEST_CASE(clock_formats_french_weekday_month) {
    SYSTEMTIME t{};
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 7;
    t.wDayOfWeek = 3;   // mercredi
    t.wHour = 14;
    t.wMinute = 32;
    t.wSecond = 5;
    CHECK(md::formatClock(t, {}) == L"mer. 7 oct. 14:32");
    t.wMonth = 2;
    t.wDayOfWeek = 0;
    t.wHour = 9;
    t.wMinute = 5;
    CHECK(md::formatClock(t, {}) == L"dim. 7 févr. 9:05");
    t.wMonth = 8;
    CHECK(md::formatClock(t, {false, true}) == L"7 août 9:05");
    CHECK(md::formatClock(t, {false, false}) == L"9:05");
}

TEST_CASE(clock_options_seconds_12h) {
    SYSTEMTIME t{};
    t.wMonth = 10;
    t.wDay = 7;
    t.wDayOfWeek = 3;
    t.wHour = 14;
    t.wMinute = 32;
    t.wSecond = 5;
    md::ClockOptions o{false, false, true, true};
    CHECK(md::formatClock(t, o) == L"14:32:05");
    o = {false, false, false, false};
    CHECK(md::formatClock(t, o) == L"2:32 PM");
    t.wHour = 0;
    CHECK(md::formatClock(t, o) == L"12:32 AM");
    t.wHour = 12;
    CHECK(md::formatClock(t, o) == L"12:32 PM");
}

TEST_CASE(shortcut_parses_modifiers_and_keys) {
    auto s = md::parseShortcut(L"Ctrl+Maj+S");
    REQUIRE(s.has_value());
    CHECK(s->modifiers == std::vector<WORD>({VK_CONTROL, VK_SHIFT}));
    CHECK_EQ(s->key, WORD('S'));
    CHECK_EQ(md::parseShortcut(L"Alt+F4")->key, WORD(VK_F4));
    CHECK(md::parseShortcut(L"Win+L")->modifiers == std::vector<WORD>({VK_LWIN}));
    CHECK_EQ(md::parseShortcut(L"F11")->key, WORD(VK_F11));
    CHECK(md::parseShortcut(L"F11")->modifiers.empty());
    CHECK_EQ(md::parseShortcut(L"Ctrl+Plus")->key, WORD(VK_ADD));
    CHECK_EQ(md::parseShortcut(L"Ctrl+Moins")->key, WORD(VK_SUBTRACT));
    CHECK_EQ(md::parseShortcut(L"Ctrl+,")->key, WORD(VK_OEM_COMMA));
    CHECK_EQ(md::parseShortcut(L"Win+.")->key, WORD(VK_OEM_PERIOD));
    CHECK_EQ(md::parseShortcut(L"Alt+←")->key, WORD(VK_LEFT));
    CHECK_EQ(md::parseShortcut(L"Alt+↑")->key, WORD(VK_UP));
    CHECK_EQ(md::parseShortcut(L"Ctrl+Maj+Échap")->key, WORD(VK_ESCAPE));
    CHECK_EQ(md::parseShortcut(L"Ctrl+0")->key, WORD('0'));
    CHECK_EQ(md::parseShortcut(L"ctrl+z")->key, WORD('Z'));   // casse indifférente
}

TEST_CASE(shortcut_inputs_release_in_reverse) {
    auto in = md::shortcutInputs(*md::parseShortcut(L"Ctrl+Maj+S"));
    REQUIRE(in.size() == 6);
    const WORD order[] = {VK_CONTROL, VK_SHIFT, 'S', 'S', VK_SHIFT, VK_CONTROL};
    for (std::size_t i = 0; i < 6; ++i) {
        CHECK(in[i].type == INPUT_KEYBOARD);
        CHECK_EQ(in[i].ki.wVk, order[i]);
        CHECK_EQ(bool(in[i].ki.dwFlags & KEYEVENTF_KEYUP), i >= 3);
    }
    // Touches étendues : flèches et Windows.
    auto arrow = md::shortcutInputs(*md::parseShortcut(L"Win+←"));
    REQUIRE(arrow.size() == 4);
    CHECK(arrow[0].ki.dwFlags & KEYEVENTF_EXTENDEDKEY);
    CHECK(arrow[1].ki.dwFlags & KEYEVENTF_EXTENDEDKEY);
}

TEST_CASE(shortcut_rejects_unknown) {
    CHECK(!md::parseShortcut(L"").has_value());
    CHECK(!md::parseShortcut(L"Ctrl+").has_value());
    CHECK(!md::parseShortcut(L"Ctrl+Maj").has_value());   // pas de touche
    CHECK(!md::parseShortcut(L"Hyper+K").has_value());
    CHECK(!md::parseShortcut(L"Ctrl+Bidule").has_value());
}

TEST_CASE(foreground_ignores_shell_and_dock) {
    using K = md::ForegroundKind;
    CHECK(md::classifyForeground(L"Shell_TrayWnd", L"explorer.exe", false) == K::Ignore);
    CHECK(md::classifyForeground(L"XamlExplorerHostIslandWindow", L"explorer.exe", false) == K::Ignore);   // Alt+Tab
    CHECK(md::classifyForeground(L"MacDockMenu", L"MacDock.exe", false) == K::Ignore);
    CHECK(md::classifyForeground(L"Windows.UI.Core.CoreWindow", L"StartMenuExperienceHost.exe", false) == K::Ignore);
    CHECK(md::classifyForeground(L"Windows.UI.Core.CoreWindow", L"SearchHost.exe", false) == K::Ignore);
    CHECK(md::classifyForeground(L"#32768", L"notepad.exe", false) == K::Ignore);   // menu contextuel
    CHECK(md::classifyForeground(L"Chrome_WidgetWin_1", L"chrome.exe", true) == K::Ignore);   // notre processus
}

TEST_CASE(foreground_desktop_is_explorer) {
    using K = md::ForegroundKind;
    CHECK(md::classifyForeground(L"Progman", L"explorer.exe", false) == K::Explorer);
    CHECK(md::classifyForeground(L"WorkerW", L"explorer.exe", false) == K::Explorer);
    CHECK(md::classifyForeground(L"CabinetWClass", L"EXPLORER.EXE", false) == K::Explorer);
}

TEST_CASE(foreground_app) {
    CHECK(md::classifyForeground(L"Chrome_WidgetWin_1", L"chrome.exe", false) == md::ForegroundKind::App);
    CHECK(md::classifyForeground(L"Notepad", L"notepad.exe", false) == md::ForegroundKind::App);
}

TEST_CASE(menubar_settings_roundtrip) {
    md::MenuBarSettings s;
    s.autohide = true;
    s.font = L"Inter";
    s.clock.seconds = true;
    s.clock.hour24 = false;
    s.showSound = false;
    s.metrics.height = 30;
    auto back = md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s));
    CHECK(back.autohide);
    CHECK(back.font == L"Inter");
    CHECK(back.clock.seconds);
    CHECK(!back.clock.hour24);
    CHECK(back.clock.weekday);
    CHECK(!back.showSound);
    CHECK_NEAR(back.metrics.height, 30, 1e-9);
    CHECK_NEAR(md::menuBarSettingsToJson(s).find("version")->asNumber(0), 1, 1e-9);
}

TEST_CASE(menubar_settings_clamped) {
    auto v = md::json::parse(R"({"metrics": {"height": 500, "fontSize": -3, "titlePadding": "abc", "statusWidth": 1e9},
                                 "clock": "n'importe quoi", "autohide": 3})");
    REQUIRE(v.has_value());
    auto s = md::menuBarSettingsFromJson(*v);
    CHECK_NEAR(s.metrics.height, 48, 1e-9);
    CHECK_NEAR(s.metrics.fontSize, 9, 1e-9);
    CHECK_NEAR(s.metrics.titlePadding, 10, 1e-9);   // valeur invalide : défaut
    CHECK_NEAR(s.metrics.statusWidth, 60, 1e-9);
    CHECK(s.clock.weekday && s.clock.date && !s.clock.seconds && s.clock.hour24);
    CHECK(!s.autohide);
    // Objet vide ou pas un objet : tout par défaut.
    auto d = md::menuBarSettingsFromJson(md::json::Value(42));
    CHECK_NEAR(d.metrics.height, 24, 1e-9);
    CHECK(d.showSound);
}

// ---- Menus de la barre ----
#include "../src/menubar/app_menus.h"

namespace {
const md::MenuItem* findItem(const md::BarMenu& menu, const std::wstring& text) {
    for (auto& it : menu.model.items)
        if (it.text == text) return &it;
    return nullptr;
}
const md::BarMenu* findMenu(const md::BarMenus& b, const std::wstring& title) {
    for (auto& m : b.menus)
        if (m.title == title) return &m;
    return nullptr;
}
md::MenuAction actionOf(const md::BarMenus& b, const md::MenuItem* it) {
    if (!it) return {};
    auto f = b.actions.find(it->id);
    return f == b.actions.end() ? md::MenuAction{} : f->second;
}
md::BarContext appContext() {
    md::BarContext c;
    c.appName = L"Notes";
    c.userName = L"Camille";
    return c;
}
} // namespace

TEST_CASE(menus_logo_has_system_actions) {
    auto b = md::buildBarMenus(appContext());
    REQUIRE(!b.menus.empty());
    const auto& logo = b.menus[0];
    CHECK(logo.logo);
    CHECK(actionOf(b, findItem(logo, L"Éteindre…")).kind == md::ActionKind::Shutdown);
    CHECK(actionOf(b, findItem(logo, L"Redémarrer…")).kind == md::ActionKind::Restart);
    CHECK(actionOf(b, findItem(logo, L"Suspendre")).kind == md::ActionKind::Sleep);
    auto* lock = findItem(logo, L"Verrouiller l'écran");
    REQUIRE(lock != nullptr);
    CHECK(lock->shortcut == L"Win+L");
    CHECK(actionOf(b, lock).kind == md::ActionKind::Lock);
    CHECK(actionOf(b, findItem(logo, L"Fermer la session de Camille…")).kind == md::ActionKind::SignOut);
    auto about = actionOf(b, findItem(logo, L"À propos de ce PC"));
    CHECK(about.kind == md::ActionKind::OpenUri);
    CHECK(about.arg == L"ms-settings:about");
    // Les Réglages de MacDock (l'app), juste après ceux de Windows.
    auto settings = actionOf(b, findItem(logo, L"Réglages MacDock…"));
    CHECK(settings.kind == md::ActionKind::OpenSettings);
    CHECK(settings.arg.empty());   // la section par défaut de l'app
}

TEST_CASE(menus_app_named_and_bold) {
    auto b = md::buildBarMenus(appContext());
    REQUIRE(b.menus.size() == 7);   // logo, app, Fichier, Édition, Présentation, Fenêtre, Aide
    CHECK(b.menus[1].bold);
    CHECK(b.menus[1].title == L"Notes");
    CHECK(actionOf(b, findItem(b.menus[1], L"Quitter Notes")).kind == md::ActionKind::QuitApp);
    CHECK(actionOf(b, findItem(b.menus[1], L"Masquer Notes")).kind == md::ActionKind::HideApp);
    const wchar_t* titles[] = {L"Fichier", L"Édition", L"Présentation", L"Fenêtre", L"Aide"};
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(b.menus[i + 2].title == titles[i]);
        CHECK(!b.menus[i + 2].bold);
    }
    auto save = findItem(b.menus[2], L"Enregistrer");
    REQUIRE(save != nullptr);
    CHECK(save->shortcut == L"Ctrl+S");
    auto a = actionOf(b, save);
    CHECK(a.kind == md::ActionKind::Shortcut);
    CHECK(a.arg == L"Ctrl+S");
}

TEST_CASE(menus_generic_shortcuts_parse) {
    md::BarContext explorer = appContext();
    explorer.appName = L"Explorateur";
    explorer.explorer = true;
    for (const auto& ctx : {appContext(), explorer}) {
        auto b = md::buildBarMenus(ctx);
        std::set<int> ids;
        std::function<void(const std::vector<md::MenuItem>&)> check = [&](const std::vector<md::MenuItem>& items) {
            for (auto& it : items) {
                if (it.separator()) continue;
                if (!it.submenu.empty()) {   // ouvre un sous-menu : pas d'action propre
                    check(it.submenu);
                    continue;
                }
                CHECK(ids.insert(it.id).second);                         // identifiants uniques
                CHECK(b.actions.count(it.id) == 1 || !it.enabled);       // chaque entrée active a son action
                if (!it.shortcut.empty()) CHECK(md::parseShortcut(it.shortcut).has_value());
                auto a = b.actions[it.id];
                if (a.kind == md::ActionKind::Shortcut) CHECK(md::parseShortcut(a.arg).has_value());
            }
        };
        for (auto& m : b.menus) check(m.model.items);
    }
}

TEST_CASE(menus_explorer_has_go_menu) {
    md::BarContext c = appContext();
    c.appName = L"Explorateur";
    c.explorer = true;
    auto b = md::buildBarMenus(c);
    auto* go = findMenu(b, L"Aller");
    REQUIRE(go != nullptr);
    auto dl = actionOf(b, findItem(*go, L"Téléchargements"));
    CHECK(dl.kind == md::ActionKind::GoTo);
    CHECK(dl.arg == L"shell:Downloads");
    CHECK(findItem(b.menus[1], L"Quitter Explorateur") == nullptr);   // comme le Finder : on ne le quitte pas
    CHECK(actionOf(b, findItem(b.menus[1], L"Vider la Corbeille…")).kind == md::ActionKind::EmptyTrash);
    CHECK(findMenu(b, L"Fichier") != nullptr && findMenu(b, L"Fenêtre") != nullptr);
}

TEST_CASE(menus_desktop_close_disabled) {
    md::BarContext c = appContext();
    c.appName = L"Explorateur";
    c.explorer = true;
    c.desktop = true;   // Alt+F4 sur le bureau ouvrirait la boîte d'arrêt de Windows
    auto b = md::buildBarMenus(c);
    auto* close = findItem(*findMenu(b, L"Fichier"), L"Fermer la fenêtre");
    REQUIRE(close != nullptr);
    CHECK(!close->enabled);
    CHECK(!findItem(*findMenu(b, L"Aller"), L"Précédent")->enabled);
    auto app = md::buildBarMenus(appContext());
    auto* appClose = findItem(*findMenu(app, L"Fichier"), L"Fermer la fenêtre");
    REQUIRE(appClose != nullptr);
    CHECK(appClose->enabled);
    CHECK(actionOf(app, appClose).kind == md::ActionKind::CloseWindow);
}

TEST_CASE(menus_window_list_checks_active) {
    md::BarContext c = appContext();
    c.windows = {{1, L"Courses"}, {2, L""}};
    c.activeWindow = 2;
    auto b = md::buildBarMenus(c);
    auto* win = findMenu(b, L"Fenêtre");
    REQUIRE(win != nullptr);
    auto* first = findItem(*win, L"Courses");
    auto* second = findItem(*win, L"(sans titre)");
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(!first->checked);
    CHECK(second->checked);
    auto a = actionOf(b, first);
    CHECK(a.kind == md::ActionKind::ActivateWindow);
    CHECK_EQ(a.window, std::uint64_t(1));
    CHECK(actionOf(b, findItem(*win, L"Réduire")).kind == md::ActionKind::Minimize);
}

// ---- Actions de la barre ----
#include "../src/menubar/bar_actions.h"

TEST_CASE(bar_target_window_kept_from_open) {
    HWND notes = reinterpret_cast<HWND>(std::uintptr_t(0x1010));
    HWND dockMenu = reinterpret_cast<HWND>(std::uintptr_t(0x2020));
    HWND folder = reinterpret_cast<HWND>(std::uintptr_t(0x3030));
    md::BarTarget t{notes, L"notes.exe"};
    // Le menu de la barre ou du Dock prend le premier plan : la commande ira toujours à l'app.
    auto kept = md::keepTarget(t, dockMenu, md::ForegroundKind::Ignore, L"");
    CHECK(kept.window == notes);
    CHECK(kept.appId == L"notes.exe");
    auto moved = md::keepTarget(t, folder, md::ForegroundKind::Explorer, L"explorer");
    CHECK(moved.window == folder);
    CHECK(moved.appId == L"explorer");
    auto app = md::keepTarget(moved, notes, md::ForegroundKind::App, L"notes.exe");
    CHECK(app.window == notes);
}

TEST_CASE(bar_actions_system_confirmed) {
    int restarts = 0, shutdowns = 0, signOuts = 0, sleeps = 0, locks = 0, questions = 0;
    bool answer = false;
    md::SystemActions sys;
    sys.restart = [&] { ++restarts; };
    sys.shutdown = [&] { ++shutdowns; };
    sys.signOut = [&] { ++signOuts; };
    sys.sleep = [&] { ++sleeps; };
    sys.lock = [&] { ++locks; };
    sys.confirm = [&](const std::wstring& q) { ++questions; CHECK(!q.empty()); return answer; };
    md::ActionContext ctx;   // pas de fenêtre cible : les actions système n'en ont pas besoin
    for (auto k : {md::ActionKind::Restart, md::ActionKind::Shutdown, md::ActionKind::SignOut})
        md::runAction({k}, ctx, sys);
    CHECK_EQ(questions, 3);
    CHECK_EQ(restarts + shutdowns + signOuts, 0);   // refusé : rien
    answer = true;
    for (auto k : {md::ActionKind::Restart, md::ActionKind::Shutdown, md::ActionKind::SignOut})
        md::runAction({k}, ctx, sys);
    CHECK_EQ(restarts, 1);
    CHECK_EQ(shutdowns, 1);
    CHECK_EQ(signOuts, 1);
    // Suspendre et verrouiller : sans confirmation, comme sur macOS.
    md::runAction({md::ActionKind::Sleep}, ctx, sys);
    md::runAction({md::ActionKind::Lock}, ctx, sys);
    CHECK_EQ(sleeps, 1);
    CHECK_EQ(locks, 1);
    CHECK_EQ(questions, 6);
}

TEST_CASE(bar_actions_ignore_missing_window) {
    // Une commande vers une fenêtre disparue ne fait rien (et ne plante pas).
    md::SystemActions sys;
    md::ActionContext ctx;
    // Handle certainement mort : une fenêtre créée puis détruite (jamais une vraie fenêtre de l'utilisateur).
    HWND dead = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(dead != nullptr);
    DestroyWindow(dead);
    REQUIRE(!IsWindow(dead));
    ctx.target.window = dead;
    CHECK(!md::runAction({md::ActionKind::Shortcut, L"Ctrl+S"}, ctx, sys));
    CHECK(!md::runAction({md::ActionKind::CloseWindow}, ctx, sys));
    CHECK(!md::runAction({md::ActionKind::Minimize}, ctx, sys));
}

TEST_CASE(bar_actions_dont_wait_for_hung_window) {
    // « Tout afficher » sur la fenêtre d'une app figée (son fil ne traite plus de messages) : la barre n'attend pas.
    HANDLE created = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE release = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HWND hung = nullptr;
    std::thread owner([&] {
        // Réduite, hors écran, jamais activée.
        hung = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"", WS_POPUP | WS_MINIMIZE, -3000, -3000,
                               10, 10, nullptr, nullptr, nullptr, nullptr);
        SetEvent(created);
        WaitForSingleObject(release, 5000);   // fil figé
        MSG m;
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&m);
        DestroyWindow(hung);
    });
    WaitForSingleObject(created, 2000);
    bool ready = hung && IsIconic(hung);
    std::vector<HWND> hidden{hung};
    md::ActionContext ctx;
    ctx.hidden = &hidden;
    md::SystemActions sys;
    const ULONGLONG t0 = GetTickCount64();
    if (ready) md::runAction({md::ActionKind::ShowAll}, ctx, sys);
    const ULONGLONG elapsed = GetTickCount64() - t0;
    SetEvent(release);
    owner.join();
    CloseHandle(created);
    CloseHandle(release);
    REQUIRE(ready);
    CHECK(elapsed < 1000);
}

// ---- Rendu ----
#include "../src/menubar/bar_renderer.h"

TEST_CASE(bar_renderer_detects_device_loss) {
    CHECK(md::isDeviceLost(DXGI_ERROR_DEVICE_REMOVED));
    CHECK(md::isDeviceLost(DXGI_ERROR_DEVICE_RESET));
    CHECK(md::isDeviceLost(DXGI_ERROR_DEVICE_HUNG));
    CHECK(md::isDeviceLost(D2DERR_RECREATE_TARGET));
    CHECK(!md::isDeviceLost(S_OK));
    CHECK(!md::isDeviceLost(E_INVALIDARG));
}

TEST_CASE(menubar_settings_hud) {
    CHECK(md::MenuBarSettings{}.hud);
    auto v = md::json::parse(R"({"hud": false})");
    REQUIRE(v.has_value());
    CHECK(!md::menuBarSettingsFromJson(*v).hud);
    md::MenuBarSettings s;
    s.hud = false;
    CHECK(!md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).hud);
}

TEST_CASE(menubar_volume_feedback_on_by_default) {
    CHECK(md::menuBarSettingsFromJson(*md::json::parse("{}")).volumeFeedback);
    CHECK(!md::menuBarSettingsFromJson(*md::json::parse("{\"volumeFeedback\":false}")).volumeFeedback);
}

TEST_CASE(bar_desktop_focus_follows_macos) {
    // Le premier plan passe au bureau. Comme macOS : un clic sur le bureau active le Finder (Explorateur) ; une app qui
    // se ferme laisse la place à la dernière app utilisée qui a encore une fenêtre, jamais à l'Explorateur par défaut.
    using F = md::DesktopFocus;
    md::DesktopFocusContext c;
    c.clickedDesktop = true;
    c.otherWindowVisible = true;
    CHECK(md::desktopFocus(c) == F::ShowExplorer);   // clic sur le bureau : le Finder, même s'il reste des fenêtres
    c = {};
    c.previousGone = true;
    c.otherWindowVisible = true;
    CHECK(md::desktopFocus(c) == F::ActivateNext);   // Claude fermé, Brave encore ouvert : Brave
    c.otherWindowVisible = false;
    CHECK(md::desktopFocus(c) == F::ShowExplorer);   // plus aucune fenêtre : le Finder
    c = {};
    c.previousMinimized = true;
    c.otherWindowVisible = true;
    CHECK(md::desktopFocus(c) == F::ActivateNext);   // réduite, d'autres fenêtres : la suivante, comme Windows
    c.otherWindowVisible = false;
    CHECK(md::desktopFocus(c) == F::KeepPrevious);   // seule fenêtre réduite : l'app reste active, comme sur Mac
    c = {};
    CHECK(md::desktopFocus(c) == F::ShowExplorer);   // le bureau pris autrement : on suit Windows
}

TEST_CASE(menus_window_move_and_resize) {
    // Plan 50 : comme dans macOS 26, le menu Fenêtre range la fenêtre active (Remplir, Centrer, moitiés, quarts).
    auto b = md::buildBarMenus(appContext());
    const md::BarMenu* w = findMenu(b, L"Fenêtre");
    REQUIRE(w != nullptr);
    CHECK(actionOf(b, findItem(*w, L"Remplir")).kind == md::ActionKind::Tile);
    CHECK(actionOf(b, findItem(*w, L"Remplir")).arg == L"fill");
    CHECK(actionOf(b, findItem(*w, L"Centrer")).arg == L"center");
    const md::MenuItem* mr = findItem(*w, L"Déplacer et redimensionner");
    REQUIRE(mr != nullptr);
    auto sub = [&](const wchar_t* text) -> md::MenuAction {
        for (auto& it : mr->submenu)
            if (it.text == text) return actionOf(b, &it);
        return {};
    };
    CHECK(sub(L"Gauche").kind == md::ActionKind::Tile && sub(L"Gauche").arg == L"left");
    CHECK(sub(L"Droite").arg == L"right");
    CHECK(sub(L"En bas à droite").arg == L"bottom-right");
    CHECK(sub(L"Revenir à la taille précédente").arg == L"previous");
    CHECK(!findItem(*w, L"Placer à gauche de l'écran"));   // remplacé par les moitiés
}
