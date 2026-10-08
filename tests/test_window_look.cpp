// Apparence macOS des fenêtres des autres apps (attributs DWM) : quelles fenêtres, quelles couleurs.
#include "minitest.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/menubar/window_look.h"

namespace {
md::LightsWindowInfo notepad() {
    md::LightsWindowInfo w;
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
