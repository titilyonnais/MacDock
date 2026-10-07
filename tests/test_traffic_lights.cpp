// Feux tricolores : éligibilité, géométrie, rendu, couleur du fond.
#include "minitest.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/traffic_lights.h"

namespace {
md::LightsWindowInfo classic() {
    md::LightsWindowInfo w;
    w.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    w.className = L"Notepad";
    w.frame = RECT{100, 100, 900, 700};
    w.client = RECT{108, 131, 892, 692};   // barre de titre de 31 px
    return w;
}
} // namespace

TEST_CASE(lights_want_classic_not_custom) {
    CHECK(md::wantsLights(classic(), md::LightsMode::Standard, 96));
    auto custom = classic();
    custom.client.top = 101;   // zone client dès le haut : barre de titre dessinée par l'app
    CHECK(!md::wantsLights(custom, md::LightsMode::Standard, 96));
    CHECK(md::wantsLights(custom, md::LightsMode::All, 96));
    CHECK(!md::wantsLights(classic(), md::LightsMode::Off, 96));
    auto hiDpi = classic();
    hiDpi.client.top = 100 + 31;   // 31 px à 200 % : moins de 20 pt
    CHECK(!md::wantsLights(hiDpi, md::LightsMode::Standard, 192));
    hiDpi.client.top = 100 + 62;
    CHECK(md::wantsLights(hiDpi, md::LightsMode::Standard, 192));
}

TEST_CASE(lights_refuse_special_windows) {
    auto tool = classic();
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(!md::wantsLights(tool, md::LightsMode::All, 96));
    auto noSys = classic();
    noSys.style &= ~WS_SYSMENU;
    CHECK(!md::wantsLights(noSys, md::LightsMode::All, 96));
    auto noCaption = classic();
    noCaption.style = WS_POPUP | WS_BORDER;
    CHECK(!md::wantsLights(noCaption, md::LightsMode::All, 96));
    auto shell = classic();
    shell.className = L"Shell_TrayWnd";
    CHECK(!md::wantsLights(shell, md::LightsMode::All, 96));
    auto mine = classic();
    mine.ownProcess = true;
    CHECK(!md::wantsLights(mine, md::LightsMode::All, 96));
    auto min = classic();
    min.iconic = true;
    CHECK(!md::wantsLights(min, md::LightsMode::All, 96));
}

TEST_CASE(lights_layout_points) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_NEAR(l.radius, 6.0, 1e-9);
    CHECK_EQ((l.circles[0].left + l.circles[0].right) / 2, 120L);   // 20 pt du bord
    CHECK_EQ((l.circles[1].left + l.circles[1].right) / 2, 140L);
    CHECK_EQ((l.circles[2].left + l.circles[2].right) / 2, 160L);
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 115L);   // milieu de la barre de 31 px (arrondi)
    CHECK_EQ(l.window.left, 104L);
    CHECK_EQ(l.window.top, 100L);
    CHECK_EQ(l.window.bottom, 131L);
    CHECK_EQ(l.window.right, 174L);   // 8 pt après la dernière pastille
    auto big = md::lightsLayout(RECT{0, 0, 1600, 1200}, RECT{16, 62, 1584, 1184}, 192);
    CHECK_NEAR(big.radius, 12.0, 1e-9);
    CHECK_EQ((big.circles[0].left + big.circles[0].right) / 2, 40L);
    auto thin = md::lightsLayout(RECT{0, 0, 800, 600}, RECT{0, 0, 800, 600}, 96);   // mode « all » sans barre : 28 pt
    CHECK_EQ(thin.window.bottom - thin.window.top, 28L);
}

TEST_CASE(lights_hit_and_command) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_EQ(md::hitLight(l, POINT{120, 115}), 0);
    CHECK_EQ(md::hitLight(l, POINT{147, 115}), 1);   // cercle élargi de 2 px
    CHECK_EQ(md::hitLight(l, POINT{160, 115}), 2);
    CHECK_EQ(md::hitLight(l, POINT{130, 115}), -1);  // entre deux pastilles
    CHECK_EQ(md::hitLight(l, POINT{170, 128}), -1);
    CHECK(md::lightCommand(0, false) == SC_CLOSE);
    CHECK(md::lightCommand(1, false) == SC_MINIMIZE);
    CHECK(md::lightCommand(2, false) == SC_MAXIMIZE);
    CHECK(md::lightCommand(2, true) == SC_RESTORE);
}

TEST_CASE(lights_dominant_color) {
    std::vector<std::uint32_t> s{0xF3F3F3, 0xF2F3F4, 0x202020, 0xF3F3F3, 0x0078D4, 0xF4F3F3};
    const std::uint32_t c = md::dominantColor(s);
    CHECK(((c >> 16) & 0xFF) >= 0xF0);
    CHECK((c & 0xFF) >= 0xF0);
    CHECK_EQ(md::dominantColor({}), 0u);
}

TEST_CASE(lights_setting) {
    auto s = md::menuBarSettingsFromJson(*md::json::parse(R"({"trafficLights":"all"})"));
    CHECK(s.trafficLights == md::LightsMode::All);
    CHECK(md::menuBarSettingsFromJson(*md::json::parse(R"({"trafficLights":"x"})")).trafficLights == md::LightsMode::Standard);
    s.trafficLights = md::LightsMode::Off;
    CHECK(md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).trafficLights == md::LightsMode::Off);
}
