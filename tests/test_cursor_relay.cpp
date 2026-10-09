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
    CHECK(r.intervalMs(false) == 100);
}

TEST_CASE(cursor_relay_takes_over_a_silent_hook) {
    // VMware au premier plan avale la souris, SetCursorPos ne passe pas par les crochets : le curseur bouge sans que
    // le crochet le voie, la position lue est livrée au Dock.
    md::CursorRelay r;
    const auto step = r.poll(sample({500, 900}, {500, 2090}, 150, 1000));
    REQUIRE(step.deliver.has_value());
    CHECK(step.deliver->x == 500 && step.deliver->y == 900);
    CHECK(r.active());
    CHECK(r.intervalMs(false) == 16);
}

TEST_CASE(cursor_relay_relays_at_once_under_an_elevated_window) {
    // App en administrateur au premier plan : le crochet d'un processus non élevé est aveugle, inutile d'attendre.
    md::CursorRelay r;
    CHECK(r.intervalMs(true) == 16);
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
    CHECK(r.intervalMs(false) == 100);
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
