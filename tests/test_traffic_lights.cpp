// Feux tricolores : éligibilité, géométrie, rendu, couleur du fond.
#include <cstdlib>

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

TEST_CASE(lights_render_colors) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    md::LightsState st;
    st.enabled[0] = st.enabled[1] = st.enabled[2] = true;
    st.patchColor = 0xF3F3F3;
    auto px = md::renderLights(l, st, 1.0);
    const int w = l.window.right - l.window.left, h = l.window.bottom - l.window.top;
    REQUIRE(px.size() == std::size_t(w * h * 4));
    auto at = [&](LONG x, LONG y) { return &px[(std::size_t(y - l.window.top) * w + (x - l.window.left)) * 4]; };
    const std::uint8_t* red = at(120, 115);
    CHECK(red[2] > 240 && red[1] < 120 && red[3] == 255);
    const std::uint8_t* yellow = at(140, 115);
    CHECK(yellow[2] > 240 && yellow[1] > 160 && yellow[0] < 80);
    const std::uint8_t* green = at(160, 115);
    CHECK(green[1] > 180 && green[2] < 80);
    const std::uint8_t* patch = at(106, 103);   // fond : couleur de la barre de titre, opaque
    CHECK(patch[3] == 255 && patch[0] == 0xF3);
    CHECK(at(l.window.right - 1, 103)[3] < at(l.window.right - 8, 103)[3]);   // fondu à droite
    st.enabled[1] = false;
    auto gray = md::renderLights(l, st, 1.0);
    const std::uint8_t* g = &gray[(std::size_t(115 - l.window.top) * w + (140 - l.window.left)) * 4];
    CHECK(std::abs(int(g[0]) - int(g[2])) < 8);   // gris
}

TEST_CASE(lights_hover_draws_symbols) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 192);
    md::LightsState st;
    st.enabled[0] = st.enabled[1] = st.enabled[2] = true;
    st.patchColor = 0xF3F3F3;
    auto plain = md::renderLights(l, st, 2.0);
    st.hover = true;
    auto hover = md::renderLights(l, st, 2.0);
    CHECK(plain != hover);
    UINT w = 0, h = 0;
    auto sheet = md::lightsSheet(w, h);
    CHECK(w > 0 && h > 0 && sheet.size() == std::size_t(w) * h * 4);
}

TEST_CASE(lights_menu_bar_not_covered) {   // relecture C1 : barre de menus classique sous la barre de titre
    auto w = classic();
    w.client.top = 151;                    // 31 px de titre + 20 px de menus
    w.captionBottom = 131;
    auto l = md::lightsLayoutFor(w, 96);
    CHECK_EQ(l.window.bottom, 131L);
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 115L);
    w.captionBottom = 0;                   // inconnu : la zone client
    CHECK_EQ(md::lightsLayoutFor(w, 96).window.bottom, 151L);
}

TEST_CASE(lights_refuse_elevated) {        // relecture C2 : messages refusés par UIPI
    auto w = classic();
    w.elevated = true;
    CHECK(!md::wantsLights(w, md::LightsMode::All, 96));
}

TEST_CASE(lights_mouse_actions) {          // relecture I2 et double-clic sur une pastille
    const bool all[3] = {true, true, true}, noZoom[3] = {true, true, false};
    CHECK(md::lightsMouse(false, 0, all) == md::LightsMouse::Press);
    CHECK(md::lightsMouse(true, 0, all) == md::LightsMouse::None);    // pas de second SC_CLOSE
    CHECK(md::lightsMouse(false, 2, noZoom) == md::LightsMouse::None);
    CHECK(md::lightsMouse(false, -1, all) == md::LightsMouse::Drag);
    CHECK(md::lightsMouse(true, -1, all) == md::LightsMouse::Zoom);
    CHECK(md::captionDoubleClick(true, false) == SC_MAXIMIZE);
    CHECK(md::captionDoubleClick(true, true) == SC_RESTORE);
    CHECK(md::captionDoubleClick(false, false) == 0u);
}
