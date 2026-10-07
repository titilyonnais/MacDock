// Coins actifs : coins réels du bureau, suivi, réglage.
#include <windows.h>

#include <vector>

#include "minitest.h"
#include "../src/interact/hot_corners.h"

using md::Corner;

TEST_CASE(hot_corners_single_screen) {
    const std::vector<RECT> mons{{0, 0, 1920, 1080}};
    CHECK(md::cornerAt({0, 0}, mons) == Corner::TopLeft);
    CHECK(md::cornerAt({1, 1}, mons) == Corner::TopLeft);   // zone de 2 px
    CHECK(!md::cornerAt({2, 0}, mons));
    CHECK(md::cornerAt({1919, 0}, mons) == Corner::TopRight);
    CHECK(md::cornerAt({0, 1079}, mons) == Corner::BottomLeft);
    CHECK(md::cornerAt({1918, 1078}, mons) == Corner::BottomRight);
    CHECK(!md::cornerAt({960, 1079}, mons));
}

TEST_CASE(hot_corners_two_screens) {
    // Écran de droite plus petit, aligné en haut : seuls comptent les coins où le pointeur bute dans les deux sens.
    const std::vector<RECT> mons{{0, 0, 1920, 1080}, {1920, 0, 1920 + 1280, 720}};
    CHECK(!md::cornerAt({1919, 0}, mons));   // haut droit du premier : l'autre écran est à côté
    CHECK(!md::cornerAt({1920, 0}, mons));   // haut gauche du second
    CHECK(!md::cornerAt({1920, 719}, mons));   // bas gauche du second : vers la gauche, le pointeur passe au premier
    CHECK(md::cornerAt({3199, 719}, mons) == Corner::BottomRight);
    CHECK(md::cornerAt({1919, 1079}, mons) == Corner::BottomRight);
    CHECK(md::cornerAt({3199, 0}, mons) == Corner::TopRight);
}

TEST_CASE(hot_corners_tracker_once_and_rearm) {
    md::HotCornerTracker t;
    CHECK(t.update(Corner::TopLeft, {0, 0}, false) == Corner::TopLeft);
    CHECK(!t.update(Corner::TopLeft, {1, 0}, false));   // toujours dedans : une seule fois
    CHECK(!t.update(std::nullopt, {10, 10}, false));    // sorti de peu : pas réarmé
    CHECK(!t.update(Corner::TopLeft, {0, 0}, false));
    CHECK(!t.update(std::nullopt, {40, 40}, false));    // loin : réarmé
    CHECK(t.update(Corner::TopLeft, {0, 0}, false) == Corner::TopLeft);
}

TEST_CASE(hot_corners_tracker_blocked) {
    md::HotCornerTracker t;
    CHECK(!t.update(Corner::BottomRight, {1919, 1079}, true));   // glisser, plein écran : rien
    CHECK(!t.update(Corner::BottomRight, {1919, 1079}, false));  // arrivé bloqué : il faut ressortir
    CHECK(!t.update(std::nullopt, {1800, 900}, false));
    CHECK(t.update(Corner::BottomRight, {1919, 1079}, false) == Corner::BottomRight);
}

TEST_CASE(hot_corners_parse) {
    CHECK(md::parseHotCornerAction(L"missionControl") == md::HotCornerAction::MissionControl);
    CHECK(md::parseHotCornerAction(L"DESKTOP") == md::HotCornerAction::Desktop);
    CHECK(md::parseHotCornerAction(L"off") == md::HotCornerAction::Off);
    CHECK(!md::parseHotCornerAction(L"launchpad"));
    for (auto a : {md::HotCornerAction::Off, md::HotCornerAction::Apps, md::HotCornerAction::ScreenSaver})
        CHECK(md::parseHotCornerAction(md::hotCornerName(a)) == a);
}
