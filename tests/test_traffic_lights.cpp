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

TEST_CASE(lights_want_classic_and_custom) {
    // Toutes les fenêtres à barre de titre : la place des pastilles (à gauche, ou sur les boutons de Windows) est
    // décidée ensuite, d'après ce que la fenêtre répond.
    CHECK(md::wantsLights(classic(), md::LightsMode::Standard, 96));
    auto custom = classic();
    custom.client.top = 101;   // zone client dès le haut : barre de titre dessinée par l'app (Chromium, Electron…)
    CHECK(md::wantsLights(custom, md::LightsMode::Standard, 96));
    CHECK(md::wantsLights(custom, md::LightsMode::All, 96));
    CHECK(!md::wantsLights(classic(), md::LightsMode::Off, 96));
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
    // Golden Gate (comme Tahoe) : pastilles de 14 pt, 23 pt de centre à centre.
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_NEAR(l.radius, 7.0, 1e-9);
    CHECK_EQ((l.circles[0].left + l.circles[0].right) / 2, 120L);   // 20 pt du bord
    CHECK_EQ((l.circles[1].left + l.circles[1].right) / 2, 143L);
    CHECK_EQ((l.circles[2].left + l.circles[2].right) / 2, 166L);
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 115L);   // milieu de la barre de 31 px (arrondi)
    CHECK_EQ(l.window.left, 104L);
    CHECK_EQ(l.window.top, 100L);
    CHECK_EQ(l.window.bottom, 131L);
    CHECK_EQ(l.window.right, 181L);   // 8 pt après la dernière pastille
    CHECK(l.lights);
    auto big = md::lightsLayout(RECT{0, 0, 1600, 1200}, RECT{16, 62, 1584, 1184}, 192);
    CHECK_NEAR(big.radius, 14.0, 1e-9);
    CHECK_EQ((big.circles[0].left + big.circles[0].right) / 2, 40L);
    auto thin = md::lightsLayout(RECT{0, 0, 800, 600}, RECT{0, 0, 800, 600}, 96);   // mode « all » sans barre : 28 pt
    CHECK_EQ(thin.window.bottom - thin.window.top, 28L);
}

TEST_CASE(lights_hit_and_command) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    CHECK_EQ(md::hitLight(l, POINT{120, 115}), 0);
    CHECK_EQ(md::hitLight(l, POINT{151, 115}), 1);   // cercle élargi de 2 px
    CHECK_EQ(md::hitLight(l, POINT{166, 115}), 2);
    CHECK_EQ(md::hitLight(l, POINT{131, 115}), -1);  // entre deux pastilles
    CHECK_EQ(md::hitLight(l, POINT{178, 128}), -1);
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
    // Verre Golden Gate : teintes désaturées, plus claires en haut (reflet) qu'au centre.
    const std::uint8_t* red = at(120, 116);
    CHECK(red[2] > 200 && red[1] < 130 && red[3] == 255);
    const std::uint8_t* yellow = at(143, 116);
    CHECK(yellow[2] > 200 && yellow[1] > 150 && yellow[0] < 110);
    const std::uint8_t* green = at(166, 116);
    CHECK(green[1] > 160 && green[2] < 130);
    CHECK(at(120, 110)[1] > red[1]);   // reflet du haut
    const std::uint8_t* patch = at(106, 103);   // fond : couleur de la barre de titre, opaque
    CHECK(patch[3] == 255 && patch[0] == 0xF3);
    CHECK(at(l.window.right - 1, 103)[3] < at(l.window.right - 8, 103)[3]);   // fondu à droite
    st.enabled[1] = false;
    auto gray = md::renderLights(l, st, 1.0);
    const std::uint8_t* g = &gray[(std::size_t(116 - l.window.top) * w + (143 - l.window.left)) * 4];
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

namespace {
// Fenêtre façon Chromium : barre de titre dessinée par l'app ; onglets (client) à gauche, réduire / agrandir / fermer
// (46 px chacun) en haut à droite sur 30 px de haut, légende ailleurs dans la bande du haut.
LRESULT chromiumHit(POINT p) {
    const RECT frame{0, 0, 1000, 700};
    if (p.x < frame.left || p.x >= frame.right || p.y < frame.top || p.y >= frame.bottom) return HTNOWHERE;
    if (p.y < 30 && p.x >= 862) return p.x < 908 ? HTMINBUTTON : p.x < 954 ? HTMAXBUTTON : HTCLOSE;
    if (p.y < 40 && p.x < 12) return HTCAPTION;
    if (p.y < 40 && p.x >= 600) return HTCAPTION;
    return HTCLIENT;
}
// Fenêtre classique dessinée par Windows : icône système, puis légende ; boutons DWM.
LRESULT classicHit(POINT p) {
    if (p.y < 31) return p.x < 130 ? HTSYSMENU : HTCAPTION;
    return HTCLIENT;
}
} // namespace

TEST_CASE(lights_find_buttons_drawn_by_the_app) {
    // Pas de bornes DWM (l'app dessine ses boutons) : sondage WM_NCHITTEST depuis le bord droit.
    const RECT frame{0, 0, 1000, 700};
    const RECT b = md::captionButtons(frame, frame, RECT{}, 96, chromiumHit);
    CHECK_EQ(b.left, 862L);
    CHECK_EQ(b.right, 1000L);
    CHECK_EQ(b.top, 0L);
    CHECK(b.bottom >= 28 && b.bottom <= 32);
    // Aucune réponse de bouton : rien.
    const RECT none = md::captionButtons(frame, frame, RECT{}, 96, [](POINT) -> LRESULT { return HTCLIENT; });
    CHECK(IsRectEmpty(&none));
}

TEST_CASE(lights_find_buttons_from_dwm_bounds) {
    // Mesuré (Windows 11, 200 %) : fenêtre en 900,300 ; bornes 1296,0,1588,57 dans le repère de la fenêtre.
    const RECT window{889, 300, 2500, 1300}, frame{900, 300, 2489, 1289};
    const RECT b = md::captionButtons(window, frame, RECT{1296, 0, 1588, 57}, 192, classicHit);
    CHECK_EQ(b.left, 889L + 1296);
    CHECK_EQ(b.right, 889L + 1588);
    CHECK_EQ(b.top, 300L);
    CHECK_EQ(b.bottom, 357L);
}

TEST_CASE(lights_dwm_bounds_clipped_to_visible_frame) {
    // Mesuré (Brave agrandi, 200 %) : fenêtre -13,35,3853,2035, cadre visible 0,48,3840,2022, bornes 3560,0,3852,57.
    // Les bornes commencent dans la bordure invisible : le calque déborderait sur la barre de menus.
    const RECT window{-13, 35, 3853, 2035}, frame{0, 48, 3840, 2022};
    const RECT b = md::captionButtons(window, frame, RECT{3560, 0, 3852, 57}, 192, classicHit);
    CHECK_EQ(b.top, 48L);
    CHECK_EQ(b.bottom, 92L);
    CHECK_EQ(b.left, 3547L);
    CHECK_EQ(b.right, 3839L);
}

TEST_CASE(lights_left_free_only_over_caption) {
    // Place des pastilles libre seulement si la fenêtre y répond « légende » (ou icône système) sur toute leur
    // hauteur : jamais sur des onglets ou des menus dessinés par l'app.
    const RECT frame{0, 0, 1000, 700};
    CHECK(md::leftCaptionFree(frame, 31, 96, classicHit));
    CHECK(!md::leftCaptionFree(frame, 30, 96, chromiumHit));
    auto menuBelow = [](POINT p) -> LRESULT { return p.y < 12 ? HTCAPTION : HTCLIENT; };
    CHECK(!md::leftCaptionFree(frame, 31, 96, menuBelow));   // menus de l'app dans la barre (VS Code…)
}

TEST_CASE(lights_over_windows_buttons) {
    // Pas de place à gauche : les pastilles prennent la place des boutons de Windows, qu'elles cachent.
    const RECT buttons{862, 0, 1000, 30};
    const auto l = md::lightsOverButtons(buttons, 96);
    CHECK(EqualRect(&l.window, &buttons));
    CHECK(l.lights);
    const LONG c0 = (l.circles[0].left + l.circles[0].right) / 2, c2 = (l.circles[2].left + l.circles[2].right) / 2;
    CHECK_EQ(c2 - c0, 46L);                                     // 23 pt d'écart
    CHECK(c0 - buttons.left >= 7 && buttons.right - c2 >= 7);   // dans la zone
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 15L);   // centrées verticalement
    // Le haut reste transparent : on peut toujours redimensionner par le bord.
    md::LightsState st;
    st.patchColor = 0x202020;
    auto px = md::renderLights(l, st, 1.0);
    CHECK_EQ(int(px[3]), 0);                                        // coin haut gauche, bord du haut
    CHECK_EQ(int(px[(std::size_t(20) * 138 + 2) * 4 + 3]), 255);    // plus bas : fond opaque
}

TEST_CASE(lights_zoomed_cover_buttons_to_the_top) {
    // Fenêtre agrandie : pas de bord à redimensionner, et une bande transparente laisserait cliquer (ou survoler,
    // menu Snap) les vrais boutons de Windows juste au-dessus des pastilles.
    const RECT buttons{862, 0, 1000, 30};
    CHECK_EQ(md::lightsOverButtons(buttons, 96, true).topGap, 0L);
    CHECK_EQ(md::buttonsCover(buttons, 96, true).topGap, 0L);
    CHECK(md::lightsOverButtons(buttons, 96).topGap > 0);
}

TEST_CASE(lights_cover_hides_buttons_without_lights) {
    // Pastilles à gauche : un cache de la couleur de la barre de titre recouvre les boutons de Windows.
    const RECT buttons{862, 0, 1000, 30};
    auto c = md::buttonsCover(buttons, 96);
    CHECK(!c.lights);
    md::LightsState st;
    st.patchColor = 0xF3F3F3;
    auto px = md::renderLights(c, st, 1.0);
    const std::uint8_t* mid = &px[(std::size_t(15) * 138 + 69) * 4];
    CHECK(mid[3] == 255 && mid[0] == 0xF3);
}

TEST_CASE(lights_pressed_is_darker) {
    auto l = md::lightsLayout(RECT{100, 100, 900, 700}, RECT{108, 131, 892, 692}, 96);
    md::LightsState st;
    st.patchColor = 0xF3F3F3;
    const int w = l.window.right - l.window.left;
    auto normal = md::renderLights(l, st, 1.0);
    st.pressed = 0;
    auto pressed = md::renderLights(l, st, 1.0);
    const std::size_t i = (std::size_t(116 - l.window.top) * w + (120 - l.window.left)) * 4;
    CHECK(pressed[i + 2] + 20 < normal[i + 2]);   // rouge plus sombre sous le doigt
}
