// Apparence macOS des fenêtres des autres apps (attributs DWM) : quelles fenêtres, quelles couleurs.
#include "minitest.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/window_look.h"
#include "../src/core/own_lights.h"

namespace {
md::WindowInfo notepad() {
    md::WindowInfo w;
    w.style = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
    w.className = L"Notepad";
    w.frame = RECT{100, 100, 900, 700};
    w.client = RECT{108, 131, 892, 692};   // barre de titre dessinée par Windows (31 px)
    return w;
}
} // namespace

TEST_CASE(window_look_classic_caption_gets_mac_colors) {
    const auto light = md::macWindowLook(notepad(), false, 96);
    REQUIRE(light.has_value());
    CHECK(light->round);
    CHECK(light->noBorder);
    REQUIRE(light->caption);
    CHECK(GetRValue(light->captionColor) >= 225);   // gris très clair, comme une barre de titre macOS
    CHECK(GetRValue(light->textColor) <= 60);
    const auto dark = md::macWindowLook(notepad(), true, 96);
    REQUIRE(dark.has_value());
    CHECK(GetRValue(dark->captionColor) <= 60);
    CHECK(GetRValue(dark->textColor) >= 200);
}

TEST_CASE(window_look_custom_caption_keeps_its_colors) {
    auto chrome = notepad();
    chrome.client.top = chrome.frame.top;   // barre dessinée par l'app : on ne touche pas à ses couleurs
    const auto l = md::macWindowLook(chrome, false, 96);
    REQUIRE(l.has_value());
    CHECK(l->round);
    CHECK(l->noBorder);
    CHECK(!l->caption);
}

TEST_CASE(window_look_skips_special_windows) {
    auto tool = notepad();
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(!md::macWindowLook(tool, false, 96).has_value());
    auto popup = notepad();
    popup.style = WS_POPUP | WS_VISIBLE;
    CHECK(!md::macWindowLook(popup, false, 96).has_value());
    auto mine = notepad();
    mine.ownProcess = true;
    CHECK(!md::macWindowLook(mine, false, 96).has_value());
    auto elevated = notepad();
    elevated.elevated = true;
    CHECK(!md::macWindowLook(elevated, false, 96).has_value());
}

TEST_CASE(window_look_skips_macdock_own_lights) {   // plan 56 : l'app Réglages garde son apparence
    auto w = notepad();
    w.ownLights = true;
    CHECK(!md::macWindowLook(w, false, 96).has_value());
}

// Fenêtre qui couvre tout son écran sans être agrandie (vidéo en plein écran dans Chrome, jeu sans bordure) : coins
// rendus à Windows, sinon l'écran entier garde quatre coins arrondis sur fond noir.
TEST_CASE(window_look_leaves_fullscreen_windows_square) {
    auto video = notepad();
    video.monitor = RECT{0, 0, 1920, 1080};
    video.frame = video.monitor;
    video.client = video.monitor;
    CHECK(!md::macWindowLook(video, false, 96).has_value());
    auto maximized = video;   // agrandie sous une barre masquée : elle garde son apparence
    maximized.zoomed = true;
    CHECK(md::macWindowLook(maximized, false, 96).has_value());
    auto windowed = notepad();
    windowed.monitor = RECT{0, 0, 1920, 1080};
    CHECK(md::macWindowLook(windowed, false, 96).has_value());
}

TEST_CASE(window_look_square_in_fullscreen) {   // relecture du plan 72 : plein écran de macOS sans coins arrondis
    auto full = notepad();
    full.monitor = RECT{0, 0, 1920, 1080};
    full.frame = full.monitor;
    CHECK(md::squareInFullscreen(full));
    full.zoomed = true;   // agrandie : Windows la carre déjà
    CHECK(!md::squareInFullscreen(full));
    auto windowed = notepad();
    windowed.monitor = RECT{0, 0, 1920, 1080};
    CHECK(!md::squareInFullscreen(windowed));
    auto video = full;   // sans barre de titre (vidéo, jeu) : jamais touchée
    video.zoomed = false;
    video.style = WS_POPUP | WS_VISIBLE;
    CHECK(!md::squareInFullscreen(video));
}

TEST_CASE(window_look_setting_defaults_on_and_roundtrips) {
    md::MenuBarSettings s = md::menuBarSettingsFromJson(md::json::Value(md::json::Object{}));
    CHECK(s.macWindows);
    s.macWindows = false;
    CHECK(!md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).macWindows);
}

TEST_CASE(window_look_respects_app_corner_choice) {
    // Une app qui a demandé des coins carrés ou petits (valeur lue avant notre passage) les garde.
    CHECK(md::shouldRoundCorners(0));    // par défaut
    CHECK(md::shouldRoundCorners(2));    // déjà ronds
    CHECK(!md::shouldRoundCorners(1));   // DWMWCP_DONOTROUND
    CHECK(!md::shouldRoundCorners(3));   // DWMWCP_ROUNDSMALL
}

TEST_CASE(window_look_leaves_system_backdrop_captions) {
    // Bloc-notes de Windows 11 : barre en Mica (DWMWA_SYSTEMBACKDROP_TYPE = 4) ; une couleur de légende la rendrait
    // gris-bleu. Coins et liseré seulement.
    const auto mica = md::macWindowLook(notepad(), false, 96, 4);
    REQUIRE(mica.has_value());
    CHECK(mica->round);
    CHECK(!mica->caption);
    CHECK(md::macWindowLook(notepad(), false, 96, 1)->caption);   // DWMSBT_NONE : barre classique
    CHECK(md::macWindowLook(notepad(), false, 96, 0)->caption);   // auto (Win32 classique)
}

// Fenêtres habillées (reprend les tests de l'éligibilité des pastilles, retirées au plan 74).
TEST_CASE(window_look_styles_classic_and_custom) {
    CHECK(md::styledWindow(notepad()));
    auto custom = notepad();
    custom.client.top = 101;   // zone client dès le haut : barre de titre dessinée par l'app (Chromium, Electron…)
    CHECK(md::styledWindow(custom));
    auto electron = custom;   // Electron sans cadre (app Claude) : barre de titre et boutons, pas de menu système
    electron.style = WS_VISIBLE | WS_CLIPSIBLINGS | WS_MAXIMIZE | WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    electron.className = L"Chrome_WidgetWin_1";
    CHECK(md::styledWindow(electron));
}

TEST_CASE(window_look_styled_window_rules) {
    auto tool = notepad();
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(!md::styledWindow(tool));
    auto noSys = notepad();
    noSys.style &= ~(WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    CHECK(!md::styledWindow(noSys));
    auto noCaption = notepad();
    noCaption.style = WS_POPUP | WS_BORDER;
    CHECK(!md::styledWindow(noCaption));
    auto shell = notepad();
    shell.className = L"Shell_TrayWnd";
    CHECK(!md::styledWindow(shell));
    auto mine = notepad();
    mine.ownProcess = true;
    CHECK(!md::styledWindow(mine));
    auto min = notepad();
    min.iconic = true;
    CHECK(!md::styledWindow(min));
    auto elevated = notepad();
    elevated.elevated = true;
    CHECK(!md::styledWindow(elevated));
    auto own = notepad();   // Réglages MacDock, Forcer à quitter : leur cadre à elles (plan 56)
    own.ownLights = true;
    CHECK(!md::styledWindow(own));
}

TEST_CASE(window_look_own_lights_mark_on_a_real_window) {   // plan 56 : la propriété posée, lue puis retirée
    const HWND h = CreateWindowExW(0, L"STATIC", L"essai", 0, 0, 0, 10, 10, HWND_MESSAGE, nullptr, nullptr, nullptr);
    REQUIRE(h != nullptr);
    CHECK(!md::drawsOwnLights(h));
    md::markOwnLights(h);
    CHECK(md::drawsOwnLights(h));
    md::unmarkOwnLights(h);
    CHECK(!md::drawsOwnLights(h));
    DestroyWindow(h);
}

TEST_CASE(window_look_own_frame_round_without_border) {   // relecture du plan 56 : le liseré de Windows ne revient pas
    const HWND h = CreateWindowExW(0, L"STATIC", L"essai", WS_OVERLAPPEDWINDOW, 0, 0, 120, 80, nullptr, nullptr, nullptr,
                                   nullptr);
    REQUIRE(h != nullptr);
    CHECK(md::applyOwnFrame(h));
    DWORD corner = 0;
    CHECK(SUCCEEDED(DwmGetWindowAttribute(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner)));
    CHECK(corner == 2);   // DWMWCP_ROUND
    DestroyWindow(h);
}

TEST_CASE(window_look_small_corners_for_small_windows) {
    // Plan 74 : un grand rayon (26 pt) sur une petite fenêtre (Exécuter, alertes) rend mal ; comme macOS, qui arrondit
    // moins les fenêtres sans barre d'outils : petits coins (petit rayon du mod de Windhawk, 12 pt).
    auto run = notepad();   // « Exécuter » : boîte de dialogue de taille fixe
    run.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE;
    run.frame = RECT{100, 100, 520, 310};
    CHECK(md::useSmallCorners(run, 96, 12));
    CHECK(!md::useSmallCorners(notepad(), 96, 12));   // fenêtre ordinaire, grande : grand rayon
    auto tiny = notepad();   // redimensionnable mais petite
    tiny.frame = RECT{100, 100, 380, 300};
    CHECK(md::useSmallCorners(tiny, 96, 12));
    CHECK(md::useSmallCorners(tiny, 192, 12));   // 280 × 200 px à 200 % : 140 × 100 pt, petite aussi
    auto big = notepad();   // 800 × 600 px à 200 % : 400 × 300 pt, ordinaire
    big.frame = RECT{0, 0, 800, 650};
    CHECK(!md::useSmallCorners(big, 192, 12));
    CHECK(!md::useSmallCorners(run, 96, 0));   // sans le mod : petits coins de Windows (4 pt), pires que les 8 pt
    CHECK(!md::useSmallCorners(run, 96, 6));   // petit rayon du mod trop petit pour une fenêtre
}
