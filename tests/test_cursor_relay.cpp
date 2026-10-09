#include <windows.h>

#include "minitest.h"
#include "../src/interact/cursor_relay.h"

namespace {

md::CursorSample sample(POINT cursor, POINT known, unsigned long long hookAgeMs, unsigned long long nowMs,
                        bool elevated = false) {
    md::CursorSample s;
    s.cursor = cursor;
    s.known = known;
    s.hookAgeMs = hookAgeMs;
    s.nowMs = nowMs;
    s.elevatedForeground = elevated;
    return s;
}

} // namespace

TEST_CASE(cursor_relay_leaves_a_live_hook_alone) {
    md::CursorRelay r;
    const auto step = r.poll(sample({500, 900}, {480, 900}, 10, 1000));   // le crochet vient de parler : il livrera
    CHECK(!step.deliver.has_value());
    CHECK(!r.active());
}

TEST_CASE(cursor_relay_takes_over_a_silent_hook) {
    // VMware au premier plan avale la souris, SetCursorPos ne passe pas par les crochets : le curseur bouge sans que
    // le crochet le voie, la position lue est livrée au Dock.
    md::CursorRelay r;
    const auto step = r.poll(sample({500, 900}, {500, 2090}, 150, 1000));
    REQUIRE(step.deliver.has_value());
    CHECK(step.deliver->x == 500 && step.deliver->y == 900);
    CHECK(r.active());
    CHECK(r.intervalMs(1000) == 16);
}

TEST_CASE(cursor_relay_relays_at_once_under_an_elevated_window) {
    // App en administrateur au premier plan : le crochet d'un processus non élevé est aveugle, inutile d'attendre.
    md::CursorRelay r;
    const auto step = r.poll(sample({500, 900}, {500, 2090}, 0, 1000, true));
    CHECK(step.deliver.has_value());
}

TEST_CASE(cursor_relay_delivers_nothing_for_a_still_cursor) {
    md::CursorRelay r;
    CHECK(r.poll(sample({500, 900}, {500, 2090}, 150, 1000)).deliver.has_value());
    // Curseur immobile depuis : rien de plus, même si la position connue diffère (bord poussé, par exemple).
    CHECK(!r.poll(sample({500, 900}, {500, 2095}, 250, 1100)).deliver.has_value());
}

TEST_CASE(cursor_relay_stops_when_the_hook_returns) {
    md::CursorRelay r;
    CHECK(r.poll(sample({500, 900}, {500, 2090}, 150, 1000)).deliver.has_value());
    CHECK(r.active());
    const auto step = r.poll(sample({520, 900}, {520, 900}, 5, 1016));   // le crochet a livré ce mouvement
    CHECK(!step.deliver.has_value());
    CHECK(!r.active());
}

TEST_CASE(cursor_relay_slows_down_when_the_mouse_rests) {
    md::CursorRelay r;
    CHECK(r.poll(sample({500, 900}, {500, 2090}, 150, 1000)).deliver.has_value());
    CHECK(!r.poll(sample({500, 900}, {500, 900}, 1200, 2100)).deliver.has_value());   // plus d'une seconde sans mouvement
    CHECK(!r.active());
}

TEST_CASE(cursor_relay_reinstalls_a_hook_silent_for_long) {
    // Muet plus de 2 s alors que la souris bouge et qu'aucune app élevée ne l'explique : crochet retiré par Windows,
    // ou un autre crochet qui ne passe pas la main ; réinstallé, il repasse en tête. Au plus une fois toutes les 10 s.
    md::CursorRelay r;
    CHECK(!r.poll(sample({500, 900}, {500, 2090}, 1500, 1000)).reinstallHook);
    CHECK(r.poll(sample({510, 900}, {500, 900}, 2100, 1600)).reinstallHook);
    CHECK(!r.poll(sample({520, 900}, {510, 900}, 2200, 1700)).reinstallHook);
    CHECK(!r.poll(sample({530, 900}, {520, 900}, 9000, 11500)).reinstallHook);
    CHECK(r.poll(sample({540, 900}, {530, 900}, 9100, 11700)).reinstallHook);
}

TEST_CASE(cursor_relay_keeps_the_hook_under_an_elevated_window) {
    md::CursorRelay r;   // aveugle à cause d'UIPI : le réinstaller n'y changerait rien
    CHECK(!r.poll(sample({500, 900}, {500, 2090}, 5000, 1000, true)).reinstallHook);
}

TEST_CASE(cursor_relay_keeps_the_hook_while_the_mouse_rests) {
    md::CursorRelay r;   // personne ne bouge la souris : le crochet n'a rien à dire
    CHECK(!r.poll(sample({500, 900}, {500, 900}, 60000, 1000)).reinstallHook);
    CHECK(!r.poll(sample({500, 900}, {500, 900}, 61000, 2000)).reinstallHook);
}

TEST_CASE(cursor_relay_polls_fast_only_after_a_move) {
    // Lecture rapide dans la seconde qui suit un mouvement, même sous une app en administrateur ; lente sinon (une app
    // élevée qui garde le premier plan toute une nuit ne doit pas réveiller le fil 60 fois par seconde).
    md::CursorRelay r;
    r.poll(sample({500, 900}, {500, 900}, 5000, 1000, true));
    CHECK(r.intervalMs(1000) == 16);
    CHECK(r.intervalMs(1900) == 16);
    r.poll(sample({500, 900}, {500, 900}, 6000, 2100, true));   // souris immobile
    CHECK(r.intervalMs(2100) == 100);
    r.poll(sample({505, 900}, {500, 900}, 7000, 3000, true));   // elle repart
    CHECK(r.intervalMs(3000) == 16);
}

TEST_CASE(cursor_relay_counts_silence_from_hook_installation) {
    // Crochet tout juste installé (démarrage, réinstallation) : son silence part de là, pas du démarrage du PC.
    CHECK(md::CursorRelay::silenceMs(5000, 0, 4500) == 500);
    CHECK(md::CursorRelay::silenceMs(5000, 4800, 4500) == 200);
    CHECK(md::CursorRelay::silenceMs(5000, 4200, 4500) == 500);
    CHECK(md::CursorRelay::silenceMs(5000, 0, 0) == 5000);
}

TEST_CASE(pointer_confined_away_from_the_dock) {
    // Une app qui confine le pointeur (ClipCursor : VMware qui prend la souris, un jeu) dans une zone qui ne contient
    // pas tout le Dock : le curseur ne peut pas y être, le Dock fait comme s'il était ailleurs.
    const RECT desktop{0, 0, 3840, 2160}, dock{900, 1950, 2940, 2160};
    CHECK(!md::pointerConfinedAway(desktop, desktop, dock));                  // libre
    CHECK(md::pointerConfinedAway(RECT{0, 0, 3840, 2027}, desktop, dock));    // fenêtre au-dessus du Dock
    CHECK(md::pointerConfinedAway(RECT{1000, 400, 2000, 2100}, desktop, dock));   // une partie du Dock seulement
    CHECK(!md::pointerConfinedAway(RECT{0, 0, 3840, 2160}, RECT{-1920, 0, 3840, 2160}, dock));   // l'écran du Dock entier
    CHECK(md::pointerConfinedAway(RECT{-1920, 0, 0, 1080}, RECT{-1920, 0, 3840, 2160}, dock));   // l'autre écran
}
