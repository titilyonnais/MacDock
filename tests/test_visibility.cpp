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

TEST_CASE(autohide_default_durations_are_tahoe) {
    // macOS 26 Tahoe : le Dock apparaît en 0,45 s (Golden Gate raccourcissait à 0,40 s).
    md::Visibility v;
    run(v, autohide(), 0, 2);
    auto edge = autohide();
    edge.cursorAtEdge = true;
    run(v, edge, 2, 2.42);
    CHECK(v.shown() < 1);   // pas encore entièrement là après 0,42 s
    run(v, edge, 2.42, 2.5);
    CHECK_EQ(v.shown(), 1.0);
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

TEST_CASE(fullscreen_stays_while_working_on_another_screen) {
    // Vidéo en plein écran sur l'écran 1, travail dans une fenêtre de l'écran 2 : l'écran 1 reste en plein écran (la
    // barre et le Dock y restent cachés), comme sur Mac. On lit l'ordre d'affichage, pas le premier plan.
    const RECT mon{0, 0, 1920, 1080};
    md::ZWindow video;
    video.rect = mon;
    video.onMonitor = true;
    md::ZWindow other;   // fenêtre de l'écran 2, au premier plan
    other.rect = {1920, 0, 3840, 1040};
    other.onMonitor = false;
    CHECK(md::fullscreenOnMonitor({other, video}, mon));
    // Une fenêtre ordinaire de cet écran passe devant la vidéo : plus de plein écran.
    md::ZWindow front;
    front.rect = {100, 100, 900, 700};
    front.onMonitor = true;
    CHECK(!md::fullscreenOnMonitor({front, video}, mon));
    // Une fenêtre toujours au-dessus qui ne couvre pas l'écran (vignette, pense-bête) ne compte pas.
    md::ZWindow pip = front;
    pip.topmost = true;
    CHECK(md::fullscreenOnMonitor({pip, video}, mon));
    // Calques transparents aux clics (superpositions AMD, NVIDIA, Discord) : ignorés, même de la taille de l'écran.
    md::ZWindow overlay = video;
    overlay.topmost = true;
    overlay.eligible = false;
    CHECK(!md::fullscreenOnMonitor({overlay, front}, mon));
    // Fenêtre agrandie à barre de titre : pas du plein écran.
    md::ZWindow maxed;
    maxed.rect = {-8, -8, 1928, 1088};
    maxed.onMonitor = true;
    maxed.zoomed = true;
    maxed.caption = true;
    CHECK(!md::fullscreenOnMonitor({maxed}, mon));
    CHECK(!md::fullscreenOnMonitor({}, mon));
}

TEST_CASE(visibility_back_at_once_after_fullscreen) {
    // Sortie du plein écran : la barre et le Dock sont là tout de suite, comme sur Mac (sinon la bande qui leur est
    // réservée reste vide le temps d'une glissade, ce qui se voit sur les bords de la fenêtre qui reprend sa taille).
    md::Visibility v;
    md::VisibilityInputs fs;
    fs.fullscreen = true;
    run(v, fs, 0, 1);
    CHECK(v.hidden());
    v.update(md::VisibilityInputs{}, 1.0 + 1.0 / 60);
    CHECK_NEAR(v.shown(), 1.0, 1e-9);
    // Masquage automatique : rien n'apparaît à la sortie si le curseur n'est pas au bord.
    md::Visibility w;
    auto fsAuto = autohide();
    fsAuto.fullscreen = true;
    run(w, fsAuto, 0, 1);
    w.update(autohide(), 1.0 + 1.0 / 60);
    CHECK(w.hidden());
}
