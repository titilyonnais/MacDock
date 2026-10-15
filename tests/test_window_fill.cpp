// Fenêtres de macOS 26 (plan 72) : Remplir au lieu d'agrandir.
#include "minitest.h"
#include "../src/menubar/menubar_settings.h"
#include "../src/shell/window_fill.h"

namespace {
const LONG kNormal = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
HWND fake(std::uintptr_t n) { return reinterpret_cast<HWND>(n); }
} // namespace

TEST_CASE(window_fill_candidates) {
    CHECK(md::fillCandidate(kNormal, 0, false));
    CHECK(!md::fillCandidate(kNormal & ~WS_THICKFRAME, 0, false));    // taille fixe
    CHECK(!md::fillCandidate(kNormal & ~WS_MAXIMIZEBOX, 0, false));   // l'app ne s'agrandit pas
    CHECK(!md::fillCandidate(kNormal & ~WS_CAPTION | WS_POPUP, 0, false));   // sans barre de titre (jeu, vidéo)
    CHECK(!md::fillCandidate(kNormal | WS_CHILD, 0, false));
    CHECK(!md::fillCandidate(kNormal, WS_EX_TOOLWINDOW, false));
    CHECK(!md::fillCandidate(kNormal, 0, true));   // fenêtres de MacDock
}

TEST_CASE(window_fill_replies) {
    using R = md::MaximizeReply;
    CHECK(md::replyToMaximize(true, true, false, false) == R::Fill);
    CHECK(md::replyToMaximize(true, true, false, true) == R::Previous);   // déjà remplie : taille d'avant (bascule)
    CHECK(md::replyToMaximize(false, true, false, false) == R::Ignore);   // réglage coupé : agrandie comme Windows
    CHECK(md::replyToMaximize(true, false, false, false) == R::Ignore);
    CHECK(md::replyToMaximize(true, true, true, false) == R::Ignore);     // l'app insiste : laissée agrandie
}

TEST_CASE(window_fill_guard) {
    md::FillGuard g;
    const HWND a = fake(0x10), b = fake(0x20);
    g.filled(a, 1000);
    CHECK(g.maximized(a, 1200));   // réagrandie aussitôt : l'app, pas un double-clic ; une fois, on réessaie
    CHECK(!g.givenUp(a));
    g.filled(a, 1210);
    CHECK(g.maximized(a, 1500));   // deux fois de suite : l'app insiste
    CHECK(g.givenUp(a));
    CHECK(!g.givenUp(b));
    g.filled(b, 1000);
    CHECK(!g.maximized(b, 2500));   // plus tard : c'est la personne (bascule vers la taille d'avant)
    g.filled(b, 4010);
    CHECK(g.maximized(b, 4100));
    CHECK(!g.maximized(b, 9000));   // la série est rompue
    CHECK(!g.givenUp(b));
    g.forget(a);
    CHECK(!g.givenUp(a));
    md::FillGuard h;
    CHECK(!h.maximized(a, 100));   // jamais remplie : rien à compter
    CHECK(!h.maximized(a, 200));
    CHECK(!h.givenUp(a));
}

TEST_CASE(window_fill_guard_slow_app_without_input) {   // relecture I3 : app qui se réagrandit après 0,7 s
    md::FillGuard g;
    const HWND a = fake(0x10);
    g.filled(a, 10000);
    CHECK(g.maximized(a, 11200, 9990));    // aucune entrée depuis notre remplissage : l'app
    g.filled(a, 11210);
    CHECK(g.maximized(a, 12400, 9990));
    CHECK(g.givenUp(a));
    md::FillGuard h;
    h.filled(a, 10000);
    CHECK(!h.maximized(a, 11200, 11100));  // double-clic de la personne après le remplissage
    h.filled(a, 20000);
    CHECK(!h.maximized(a, 23000, 9990));   // plus de 2 s : la personne (Win+Haut sans souris…)
}

TEST_CASE(zoom_tracker_reports_transitions_only) {
    md::ZoomTracker t;
    const HWND a = fake(0x10);
    CHECK(!t.changed(a, true));    // inconnue : notée (agrandie avant MacDock ou à son démarrage), rien à faire
    CHECK(!t.changed(a, true));
    CHECK(!t.changed(a, false));
    CHECK(t.changed(a, true));     // non agrandie → agrandie
    CHECK(!t.changed(a, true));    // simple déplacement d'une agrandie
    t.seed(a, false);
    CHECK(t.changed(a, true));
    t.forget(a);
    CHECK(!t.changed(a, true));
    // Relecture I1 : réduite (IsZoomed faux) puis rouverte agrandie : pas un passage à l'état agrandi.
    md::ZoomTracker r;
    r.seed(a, true);
    CHECK(!r.changed(a, false, true));
    CHECK(!r.changed(a, true));
    r.seed(a, false);
    CHECK(!r.changed(a, false, true));
    CHECK(r.changed(a, true));
}

TEST_CASE(menubar_settings_fill_on_maximize) {
    md::MenuBarSettings s = md::menuBarSettingsFromJson(md::json::Value(md::json::Object{}));
    CHECK(s.fillOnMaximize);
    s.fillOnMaximize = false;
    CHECK(!md::menuBarSettingsFromJson(md::menuBarSettingsToJson(s)).fillOnMaximize);
}
