// Masquage automatique du Dock et retrait en plein écran (logique pure).
#include "minitest.h"
#include "../src/app/visibility.h"

namespace {
// Fait avancer v de from à to par pas de 1/60 s avec les mêmes entrées.
void run(md::Visibility& v, const md::VisibilityInputs& in, double from, double to) {
    for (double t = from; t <= to + 1e-9; t += 1.0 / 60) v.update(in, t);
}
md::VisibilityInputs autohide() {
    md::VisibilityInputs in;
    in.autohide = true;
    return in;
}
} // namespace

TEST_CASE(autohide_shows_at_edge_then_hides_after_delay) {
    md::Visibility v;   // délai d'apparition 0, départ 0,5 s, animations 0,45 s
    run(v, autohide(), 0, 2);
    CHECK(v.hidden());
    auto edge = autohide();
    edge.cursorAtEdge = true;
    run(v, edge, 2, 2.2);
    CHECK(v.shown() > 0 && v.shown() < 1);   // en cours d'apparition
    run(v, edge, 2.2, 2.6);
    CHECK_EQ(v.shown(), 1.0);
    auto inDock = autohide();
    inDock.cursorInDock = true;
    run(v, inDock, 2.6, 3);
    run(v, autohide(), 3, 3.4);               // le curseur part : le Dock reste un instant
    CHECK_EQ(v.shown(), 1.0);
    CHECK(v.wakeAt() > 3.4);                  // réveil prévu à la fin du délai
    run(v, autohide(), 3.4, 4.1);
    CHECK(v.hidden());
    CHECK(v.wakeAt() < 0);
}

TEST_CASE(autohide_off_always_shown) {
    md::Visibility v;
    run(v, md::VisibilityInputs{}, 0, 3);
    CHECK_EQ(v.shown(), 1.0);
    CHECK(!v.update(md::VisibilityInputs{}, 3.1));   // rien à animer
}

TEST_CASE(fullscreen_hides_even_without_autohide) {
    md::Visibility v;
    md::VisibilityInputs fs;
    fs.fullscreen = true;
    fs.cursorInDock = true;   // même sous le curseur
    run(v, fs, 0, 0.5);
    CHECK(v.hidden());
    run(v, md::VisibilityInputs{}, 0.5, 1.0);
    CHECK_EQ(v.shown(), 1.0);
}

TEST_CASE(fullscreen_cover_detection_tolerance) {
    RECT mon{0, 0, 1920, 1080};
    CHECK(md::coversMonitor({0, 0, 1920, 1080}, mon));
    CHECK(md::coversMonitor({-8, -8, 1928, 1088}, mon));   // fenêtre maximisée sans bordure visible
    CHECK(md::coversMonitor({1, 0, 1919, 1080}, mon));     // tolérance de 1 px
    CHECK(!md::coversMonitor({2, 0, 1920, 1080}, mon));
    CHECK(!md::coversMonitor({0, 0, 1920, 1032}, mon));    // fenêtre maximisée au-dessus de la barre des tâches
    CHECK(!md::coversMonitor({1920, 0, 3840, 1080}, mon)); // autre écran
}

TEST_CASE(autohide_menu_keeps_shown) {
    md::Visibility v;
    auto menu = autohide();
    menu.menuOpen = true;
    run(v, menu, 0, 3);
    CHECK_EQ(v.shown(), 1.0);
    auto drag = autohide();
    drag.dragging = true;
    run(v, drag, 3, 5);
    CHECK_EQ(v.shown(), 1.0);
}

TEST_CASE(fullscreen_maximized_window_with_caption_is_not_fullscreen) {
    // Barre Windows masquée et pas de zone réservée : une fenêtre maximisée couvre l'écran sans être en plein écran.
    RECT mon{0, 0, 1920, 1080};
    RECT maxed{-8, -8, 1928, 1088};
    CHECK(!md::isFullscreenWindow(maxed, mon, /*zoomed*/ true, /*caption*/ true));
    CHECK(md::isFullscreenWindow(maxed, mon, true, false));      // plein écran sans bordure (jeu, vidéo)
    CHECK(md::isFullscreenWindow({0, 0, 1920, 1080}, mon, false, true));   // F11 d'un navigateur
    CHECK(!md::isFullscreenWindow({0, 0, 1920, 1000}, mon, false, false));
}
