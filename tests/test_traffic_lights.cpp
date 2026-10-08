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
    // Toutes les fenêtres à barre de titre : les pastilles prennent ensuite la place de leurs boutons, qu'ils soient
    // de Windows ou dessinés par l'app.
    CHECK(md::wantsLights(classic(), md::LightsMode::Standard, 96));
    auto custom = classic();
    custom.client.top = 101;   // zone client dès le haut : barre de titre dessinée par l'app (Chromium, Electron…)
    CHECK(md::wantsLights(custom, md::LightsMode::Standard, 96));
    CHECK(md::wantsLights(custom, md::LightsMode::All, 96));
    CHECK(!md::wantsLights(classic(), md::LightsMode::Off, 96));
    // Electron sans cadre (app Claude) : 15C70000, barre de titre et boutons, mais pas de menu système.
    auto electron = custom;
    electron.style = WS_VISIBLE | WS_CLIPSIBLINGS | WS_MAXIMIZE | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    electron.className = L"Chrome_WidgetWin_1";
    CHECK(md::wantsLights(electron, md::LightsMode::Standard, 96));
}

TEST_CASE(lights_refuse_special_windows) {
    auto tool = classic();
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(!md::wantsLights(tool, md::LightsMode::All, 96));
    auto noSys = classic();   // ni menu système, ni bouton réduire ou agrandir : rien à quoi donner des pastilles
    noSys.style &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
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
    // Golden Gate (comme Tahoe) : pastilles de 14 pt, 23 pt de centre à centre, centrées sur les trois boutons de
    // Windows qu'elles remplacent.
    const auto l = md::lightsOverButtons(RECT{862, 0, 1000, 30}, 96);
    CHECK_NEAR(l.radius, 7.0, 1e-9);
    CHECK_EQ((l.circles[0].left + l.circles[0].right) / 2, 908L);
    CHECK_EQ((l.circles[1].left + l.circles[1].right) / 2, 931L);
    CHECK_EQ((l.circles[2].left + l.circles[2].right) / 2, 954L);
    CHECK_EQ((l.circles[0].top + l.circles[0].bottom) / 2, 15L);
    const auto big = md::lightsOverButtons(RECT{1724, 0, 2000, 60}, 192);
    CHECK_NEAR(big.radius, 14.0, 1e-9);
    CHECK_EQ((big.circles[2].left + big.circles[2].right) / 2 - (big.circles[0].left + big.circles[0].right) / 2, 92L);
}

TEST_CASE(lights_layout_on_a_lone_close_button) {
    // Dialogue avec le seul bouton fermer (46 pt) : le calque s'élargit vers la gauche pour les trois pastilles
    // (réduire et zoom grisés), sans dépasser le bord droit.
    const auto l = md::lightsOverButtons(RECT{954, 0, 1000, 30}, 96);
    CHECK_EQ(l.window.right, 1000L);
    CHECK_EQ(l.window.left, 924L);   // 2 × 23 + 14 + 2 × 8 pt
    CHECK(l.circles[0].left >= l.window.left && l.circles[2].right <= l.window.right);
}

TEST_CASE(lights_hit_and_command) {
    const auto l = md::lightsOverButtons(RECT{862, 0, 1000, 30}, 96);
    CHECK_EQ(md::hitLight(l, POINT{908, 15}), 0);
    CHECK_EQ(md::hitLight(l, POINT{939, 15}), 1);   // cercle élargi de 2 px
    CHECK_EQ(md::hitLight(l, POINT{954, 15}), 2);
    CHECK_EQ(md::hitLight(l, POINT{919, 15}), -1);  // entre deux pastilles
    CHECK_EQ(md::hitLight(l, POINT{990, 28}), -1);
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
    const auto l = md::lightsOverButtons(RECT{862, 0, 1000, 30}, 96);
    md::LightsState st;
    st.enabled[0] = st.enabled[1] = st.enabled[2] = true;
    st.patchColor = 0xF3F3F3;
    auto px = md::renderLights(l, st, 1.0);
    const int w = l.window.right - l.window.left, h = l.window.bottom - l.window.top;
    REQUIRE(px.size() == std::size_t(w * h * 4));
    auto at = [&](LONG x, LONG y) { return &px[(std::size_t(y - l.window.top) * w + (x - l.window.left)) * 4]; };
    // Verre Golden Gate : teintes désaturées, plus claires en haut (reflet) qu'au centre.
    const std::uint8_t* red = at(908, 16);
    CHECK(red[2] > 200 && red[1] < 130 && red[3] == 255);
    const std::uint8_t* yellow = at(931, 16);
    CHECK(yellow[2] > 200 && yellow[1] > 150 && yellow[0] < 110);
    const std::uint8_t* green = at(954, 16);
    CHECK(green[1] > 160 && green[2] < 130);
    CHECK(at(908, 10)[1] > red[1]);   // reflet du haut
    const std::uint8_t* patch = at(866, 20);   // fond : couleur de la barre de titre, opaque, qui cache les boutons
    CHECK(patch[3] == 255 && patch[0] == 0xF3);
    CHECK_EQ(int(at(866, 1)[3]), 0);   // bord du haut transparent : la fenêtre se redimensionne par là
    st.enabled[1] = false;
    auto gray = md::renderLights(l, st, 1.0);
    const std::uint8_t* g = &gray[(std::size_t(16 - l.window.top) * w + (931 - l.window.left)) * 4];
    CHECK(std::abs(int(g[0]) - int(g[2])) < 8);   // gris
}

TEST_CASE(lights_hover_draws_symbols) {
    const auto l = md::lightsOverButtons(RECT{1724, 0, 2000, 60}, 192);
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

TEST_CASE(lights_over_windows_buttons) {
    // Les pastilles prennent la place des boutons de Windows, qu'elles cachent.
    const RECT buttons{862, 0, 1000, 30};
    const auto l = md::lightsOverButtons(buttons, 96);
    CHECK(EqualRect(&l.window, &buttons));
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
    CHECK(md::lightsOverButtons(buttons, 96).topGap > 0);
}

TEST_CASE(lights_pressed_is_darker) {
    const auto l = md::lightsOverButtons(RECT{862, 0, 1000, 30}, 96);
    md::LightsState st;
    st.patchColor = 0xF3F3F3;
    const int w = l.window.right - l.window.left;
    auto normal = md::renderLights(l, st, 1.0);
    st.pressed = 0;
    auto pressed = md::renderLights(l, st, 1.0);
    const std::size_t i = (std::size_t(16 - l.window.top) * w + (908 - l.window.left)) * 4;
    CHECK(pressed[i + 2] + 20 < normal[i + 2]);   // rouge plus sombre sous le doigt
}

TEST_CASE(lights_zoomed_frame_clipped_to_work_area) {
    // Bloc-notes agrandi (mesuré) : cadre déclaré dès y = 35, sous la barre de menus (zone de travail dès 48). Les
    // pastilles et la couleur de leur fond se prennent dans la partie visible.
    const RECT work{0, 48, 3840, 2022};
    const RECT f = md::visibleFrame(RECT{-13, 35, 3853, 2035}, work, true);
    CHECK_EQ(f.top, 48L);
    CHECK_EQ(f.left, 0L);
    CHECK_EQ(f.right, 3840L);
    const RECT normal = md::visibleFrame(RECT{100, 30, 900, 700}, work, false);   // fenêtre déplacée : telle quelle
    CHECK_EQ(normal.top, 30L);
}

TEST_CASE(lights_wait_for_zoom_animation) {
    // Agrandie ou rendue à sa taille : DWM anime le cadre (~290 ms mesurées), les pastilles attendent la fin au lieu de
    // sauter devant l'animation. Sans animation de Windows (choix de l'utilisateur), ou sans changement : tout de suite.
    CHECK(md::lightsZoomWaitMs(false, true, true) >= 280);
    CHECK(md::lightsZoomWaitMs(true, false, true) >= 280);
    CHECK_EQ(md::lightsZoomWaitMs(true, true, true), 0u);
    CHECK_EQ(md::lightsZoomWaitMs(false, true, false), 0u);
}
