// Captures d'écran façon macOS : noms de fichier, raccourcis, sélection, vignette, coins arrondis et ombre.
#include <windows.h>

#include <cstdint>
#include <set>
#include <string>

#include "minitest.h"
#include "../src/screenshot/screenshot_logic.h"

namespace {

md::BgraImage solid(int w, int h, std::uint8_t b, std::uint8_t g, std::uint8_t r) {
    md::BgraImage img;
    img.w = w;
    img.h = h;
    img.px.resize(std::size_t(w) * h * 4);
    for (std::size_t i = 0; i < img.px.size(); i += 4) {
        img.px[i] = b;
        img.px[i + 1] = g;
        img.px[i + 2] = r;
        img.px[i + 3] = 255;
    }
    return img;
}

int alphaAt(const md::BgraImage& img, int x, int y) { return img.px[(std::size_t(y) * img.w + x) * 4 + 3]; }

md::ShotKeyEvent press(unsigned vk, md::ShotMods mods) {
    md::ShotKeyEvent e;
    e.vk = vk;
    e.down = true;
    e.mods = mods;
    return e;
}

} // namespace

TEST_CASE(screenshot_names_like_a_french_mac) {
    SYSTEMTIME t{};
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 8;
    t.wHour = 9;
    t.wMinute = 5;
    t.wSecond = 7;
    CHECK(md::screenshotBaseName(t) == L"Capture d\u2019\u00e9cran 2026-10-08 \u00e0 09.05.07");
    CHECK(md::screenshotFileName(L"X", 1) == L"X.png");
    CHECK(md::screenshotFileName(L"X", 2) == L"X (2).png");
    const std::set<std::wstring> taken{L"D:\\b\\X.png", L"D:\\b\\X (2).png"};
    auto exists = [&](const std::wstring& p) { return taken.count(p) > 0; };
    CHECK(md::uniqueScreenshotPath(L"D:\\b", L"X", 1, exists) == L"D:\\b\\X (3).png");   // même seconde : numéro suivant
    CHECK(md::uniqueScreenshotPath(L"D:\\b\\", L"Y", 1, exists) == L"D:\\b\\Y.png");
    CHECK(md::uniqueScreenshotPath(L"D:\\b", L"Y", 2, exists) == L"D:\\b\\Y (2).png");   // deuxième écran
}

TEST_CASE(screenshot_keys_win_shift_3_and_4) {
    using md::ShotKey;
    const md::ShotMods ws{true, true, false, false};
    CHECK(md::screenshotKey(press('3', ws)) == ShotKey::Screen);
    CHECK(md::screenshotKey(press('4', ws)) == ShotKey::Region);
    CHECK(md::screenshotKey(press('4', {true, true, true, false})) == ShotKey::Region);   // ⌃ : presse-papiers, lu à part
    CHECK(md::screenshotKey(press('3', {true, true, false, true})) == ShotKey::Pass);    // Alt : un autre raccourci
    CHECK(md::screenshotKey(press('3', {true, false, false, false})) == ShotKey::Pass);  // ⊞3 : reste à Windows
    CHECK(md::screenshotKey(press('3', {false, true, false, false})) == ShotKey::Pass);  // # tapé
    CHECK(md::screenshotKey(press('6', ws)) == ShotKey::Pass);   // ⊞⇧5 : la barre d'outils (plus bas)
    CHECK(md::screenshotKey(press(VK_NUMPAD3, ws)) == ShotKey::Pass);
    md::ShotKeyEvent fake = press('3', ws);
    fake.injected = true;   // une app qui simule des frappes ne déclenche rien
    CHECK(md::screenshotKey(fake) == ShotKey::Pass);
    // Le relâchement (et la répétition) d'une frappe prise est avalé, même si Maj est déjà relâchée.
    md::ShotKeyEvent up = press('3', {true, false, false, false});
    up.down = false;
    up.taken = true;
    CHECK(md::screenshotKey(up) == ShotKey::Swallow);
    up.taken = false;
    CHECK(md::screenshotKey(up) == ShotKey::Pass);
    md::ShotKeyEvent again = press('4', ws);
    again.repeat = true;
    again.taken = true;
    CHECK(md::screenshotKey(again) == ShotKey::Swallow);
}

TEST_CASE(screenshot_session_keys) {
    using md::ShotSessionKey;
    CHECK(md::screenshotSessionKey(VK_ESCAPE, true, false) == ShotSessionKey::Cancel);
    CHECK(md::screenshotSessionKey(VK_ESCAPE, false, false) == ShotSessionKey::Swallow);
    CHECK(md::screenshotSessionKey(VK_SPACE, true, false) == ShotSessionKey::ToggleWindow);
    CHECK(md::screenshotSessionKey(VK_SPACE, true, true) == ShotSessionKey::Swallow);   // Espace maintenue : une bascule
    CHECK(md::screenshotSessionKey(VK_SPACE, false, false) == ShotSessionKey::Swallow);
    CHECK(md::screenshotSessionKey('A', true, false) == ShotSessionKey::Pass);
    CHECK(md::screenshotSessionKey(VK_SHIFT, true, false) == ShotSessionKey::Pass);
}

TEST_CASE(screenshot_selection_normalized_and_clamped) {
    const RECT mon{0, 0, 1920, 1080};
    RECT r = md::selectionRect({300, 200}, {100, 50}, mon);   // tirée vers le haut à gauche
    CHECK(r.left == 100 && r.top == 50 && r.right == 300 && r.bottom == 200);
    r = md::selectionRect({1800, 1000}, {2500, 1300}, mon);   // bornée à l'écran de départ
    CHECK(r.left == 1800 && r.top == 1000 && r.right == 1920 && r.bottom == 1080);
    r = md::selectionRect({100, 100}, {-50, -20}, RECT{-1920, 0, 0, 1080});
    CHECK(r.left == -50 && r.top == 0 && r.right == 0 && r.bottom == 100);
    CHECK(!md::selectionUsable(md::selectionRect({10, 10}, {12, 40}, mon)));   // un simple clic : rien
    CHECK(md::selectionUsable(md::selectionRect({10, 10}, {14, 14}, mon)));
}

TEST_CASE(screenshot_thumbnail_bottom_right_never_enlarged) {
    const RECT work{0, 0, 3840, 2088};
    RECT t = md::thumbnailRect(work, SIZE{3840, 2160}, 2.0);   // 200 × 150 points à 200 %
    CHECK(t.right == 3840 - 40 && t.bottom == 2088 - 40);
    CHECK(t.right - t.left == 400 && t.bottom - t.top == 225);
    t = md::thumbnailRect(work, SIZE{100, 50}, 2.0);   // petite zone : taille réelle
    CHECK(t.right - t.left == 100 && t.bottom - t.top == 50);
    t = md::thumbnailRect(work, SIZE{200, 3000}, 2.0);   // bande verticale : bornée en hauteur
    CHECK(t.bottom - t.top == 300 && t.right - t.left == 20);
    t = md::thumbnailRect(RECT{1920, 0, 3840, 1040}, SIZE{1920, 1080}, 1.0);   // second écran, à droite
    CHECK(t.right == 3820 && t.bottom == 1020);
}

TEST_CASE(screenshot_thumbnail_slides_from_the_right) {
    using P = md::ThumbPhase;
    CHECK(md::thumbnailOffset(P::In, 0, 300) == 300);
    CHECK(md::thumbnailOffset(P::In, md::kThumbIn, 300) == 0);
    const double mid = md::thumbnailOffset(P::In, md::kThumbIn / 2, 300);
    CHECK(mid > 0 && mid < 150);   // ralentit à l'arrivée : plus de la moitié du chemin à mi-temps
    CHECK(md::thumbnailOffset(P::Out, 0, 300) == 0);
    CHECK(md::thumbnailOffset(P::Out, md::kThumbOut, 300) == 300);
    const double out = md::thumbnailOffset(P::Out, md::kThumbOut / 2, 300);
    CHECK(out > 0 && out < 150);   // accélère au départ
}

TEST_CASE(screenshot_rounded_corners_are_transparent) {
    md::BgraImage img = solid(40, 30, 10, 20, 30);
    md::roundCorners(img, 8);
    CHECK(alphaAt(img, 0, 0) == 0 && alphaAt(img, 39, 0) == 0 && alphaAt(img, 0, 29) == 0 && alphaAt(img, 39, 29) == 0);
    CHECK(alphaAt(img, 20, 15) == 255 && alphaAt(img, 20, 0) == 255 && alphaAt(img, 0, 15) == 255);
    int partial = 0;
    for (int x = 0; x < 8; ++x)
        for (int y = 0; y < 8; ++y)
            if (alphaAt(img, x, y) > 0 && alphaAt(img, x, y) < 255) ++partial;
    CHECK(partial > 0);   // bord anti-crénelé
    CHECK(img.px[(15 * 40 + 20) * 4 + 2] == 30);   // couleur intacte
    md::BgraImage square = solid(10, 10, 0, 0, 0);
    md::roundCorners(square, 0);   // fenêtre agrandie : coins droits
    CHECK(alphaAt(square, 0, 0) == 255);
}

TEST_CASE(screenshot_window_shadow_like_macos) {
    const md::BgraImage win = solid(100, 80, 40, 50, 60);
    const md::ShadowSpec spec = md::windowShadowSpec(1.0);
    CHECK(spec.left > 0 && spec.bottom > spec.top);   // ombre plus marquée dessous
    const md::BgraImage out = md::withShadow(win, spec);
    CHECK(out.w == 100 + spec.left + spec.right && out.h == 80 + spec.top + spec.bottom);
    const int cx = spec.left + 50;
    CHECK(alphaAt(out, cx, spec.top + 40) == 255);
    CHECK(out.px[(std::size_t(spec.top + 40) * out.w + cx) * 4 + 2] == 60);   // la fenêtre garde ses couleurs
    const int below = alphaAt(out, cx, spec.top + 80 + 8), above = alphaAt(out, cx, spec.top - 8);
    CHECK(below > 0 && below < 255);
    CHECK(above < below);
    CHECK(alphaAt(out, 0, 0) < 8);   // coin de l'image : presque rien
    CHECK(md::withShadow(md::BgraImage{}, spec).w == 0);   // image vide : rien
}

TEST_CASE(screenshot_premultiplied_for_png) {
    md::BgraImage one;
    one.w = one.h = 1;
    one.px = {200, 100, 50, 128};
    const auto p = md::premultiply(one);
    CHECK(p.size() == 4 && p[0] == 100 && p[1] == 50 && p[2] == 25 && p[3] == 128);
}

TEST_CASE(screenshot_thumbnail_pixels_rounded_with_margin) {
    const md::BgraImage reduced = solid(200, 120, 90, 90, 90);
    int w = 0, h = 0, margin = 0;
    const auto px = md::thumbnailPixels(reduced, 1.0, w, h, margin);
    CHECK(margin > 0 && w == 200 + 2 * margin && h == 120 + 2 * margin);
    CHECK(px.size() == std::size_t(w) * h * 4);
    auto a = [&](int x, int y) { return int(px[(std::size_t(y) * w + x) * 4 + 3]); };
    CHECK(a(margin + 100, margin + 60) == 255);
    CHECK(a(margin, margin) < 255);   // coin de l'image arrondi
    CHECK(a(0, 0) < 8);
}

TEST_CASE(screenshot_camera_cursor_drawn_by_code) {
    const auto px = md::cameraCursorPixels(32);
    CHECK(px.size() == 32 * 32 * 4);
    int dark = 0, light = 0;
    for (std::size_t i = 0; i < px.size(); i += 4)
        if (px[i + 3] == 255) (px[i] < 64 ? dark : light) += px[i] < 64 || px[i] > 192 ? 1 : 0;
    CHECK(dark > 100 && light > 30);   // corps noir, contour blanc : visible sur tous les fonds
    CHECK(px[3] == 0);
}

TEST_CASE(screenshot_window_fully_on_screens) {
    const std::vector<RECT> one{RECT{0, 0, 1920, 1080}};
    CHECK(md::rectOnScreens(RECT{100, 100, 500, 400}, one));
    CHECK(!md::rectOnScreens(RECT{1800, 100, 2000, 400}, one));   // dépasse à droite : bande noire sinon
    // Deux écrans de hauteurs différentes côte à côte : à cheval, mais dans le vide sous le petit écran.
    const std::vector<RECT> two{RECT{0, 0, 1920, 1080}, RECT{1920, 0, 3840, 2160}};
    CHECK(md::rectOnScreens(RECT{1800, 100, 2000, 400}, two));
    CHECK(!md::rectOnScreens(RECT{1800, 1000, 2000, 1200}, two));
    CHECK(md::rectOnScreens(RECT{-1900, 10, -1000, 500}, {RECT{-1920, 0, 0, 1080}, RECT{0, 0, 1920, 1080}}));
}

TEST_CASE(screenshot_clipboard_image_flattened_on_white) {
    md::BgraImage img = solid(3, 1, 0, 0, 0);
    img.px[3] = 0;     // transparent : blanc
    img.px[7] = 128;   // noir à moitié : gris
    const md::BgraImage flat = md::flattenOn(img, 255, 255, 255);
    CHECK(flat.w == 3 && flat.h == 1);
    CHECK(flat.px[0] == 255 && flat.px[1] == 255 && flat.px[2] == 255 && flat.px[3] == 255);
    CHECK(flat.px[4] > 120 && flat.px[4] < 135 && flat.px[7] == 255);
    CHECK(flat.px[8] == 0 && flat.px[11] == 255);   // opaque : inchangé
}

TEST_CASE(recording_names_and_video_size) {
    SYSTEMTIME t{};
    t.wYear = 2026;
    t.wMonth = 10;
    t.wDay = 8;
    t.wHour = 9;
    t.wMinute = 5;
    t.wSecond = 7;
    CHECK(md::recordingBaseName(t) == L"Enregistrement de l\u2019\u00e9cran 2026-10-08 \u00e0 09.05.07");
    const std::set<std::wstring> taken{L"D:\\b\\X.mp4"};
    auto exists = [&](const std::wstring& p) { return taken.count(p) > 0; };
    CHECK(md::uniqueRecordingPath(L"D:\\b", L"Y", exists) == L"D:\\b\\Y.mp4");
    CHECK(md::uniqueRecordingPath(L"D:\\b", L"X", exists) == L"D:\\b\\X (2).mp4");
    SIZE s = md::recordingSize(SIZE{3840, 2160}, 1920);   // 4K : réduite à 1080p
    CHECK(s.cx == 1920 && s.cy == 1080);
    s = md::recordingSize(SIZE{1001, 601}, 1920);   // H.264 : dimensions paires, jamais agrandie
    CHECK(s.cx == 1000 && s.cy == 600);
    s = md::recordingSize(SIZE{5120, 1440}, 1920);
    CHECK(s.cx == 1920 && s.cy == 540);
    s = md::recordingSize(SIZE{3, 3}, 1920);   // zone minuscule : au moins 2 × 2
    CHECK(s.cx == 2 && s.cy == 2);
}

TEST_CASE(screenshot_toolbar_key_and_layout) {
    CHECK(md::screenshotKey(press('5', {true, true, false, false})) == md::ShotKey::Toolbar);   // ⊞⇧5
    const md::ShotToolbarLayout l = md::shotToolbarLayout(1.0);
    CHECK(l.buttons.size() == 7);   // ×, cinq modes, action
    CHECK(l.width > 0 && l.height > 0);
    for (std::size_t i = 0; i < l.buttons.size(); ++i) {
        const auto& b = l.buttons[i];
        CHECK(md::shotToolbarHit(l, b.x + b.w / 2, b.y + b.h / 2) == int(i));
        for (std::size_t j = i + 1; j < l.buttons.size(); ++j)
            CHECK(b.x + b.w <= l.buttons[j].x + 1e-9 || l.buttons[j].x + l.buttons[j].w <= b.x + 1e-9);
    }
    CHECK(md::shotToolbarHit(l, -5, 5) == -1);
    const md::ShotToolbarLayout big = md::shotToolbarLayout(2.0);
    CHECK(std::abs(big.width - 2 * l.width) < 1e-6);
}
