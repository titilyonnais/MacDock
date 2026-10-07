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
