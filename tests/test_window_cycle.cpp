// Fenêtre suivante de l'app au clavier (plan 59) : ⌘` de macOS, Alt + la touche sous Échap.
#include "minitest.h"
#include "../src/config/settings.h"
#include "../src/core/json.h"
#include "../src/switcher/window_cycle.h"

namespace {

md::CycleKey press(bool shift = false) {   // Alt + la touche, sans rien d'autre
    md::CycleKeyEvent e;
    e.alt = true;
    e.shift = shift;
    return md::cycleKeyAction(e);
}

std::optional<std::size_t> at(std::size_t i) { return i; }

} // namespace

TEST_CASE(window_cycle_key_is_the_key_under_escape) {   // à sa place : ` en QWERTY, ² en AZERTY, ^ en QWERTZ
    CHECK(md::isCycleKey(0x29, false));
    CHECK(!md::isCycleKey(0x29, true));    // étendue : une autre touche
    CHECK(!md::isCycleKey(0x0F, false));   // Tab
    CHECK(!md::isCycleKey(0x02, false));   // 1
}

TEST_CASE(window_cycle_key_needs_alt_alone) {
    using K = md::CycleKey;
    CHECK(press() == K::Next);
    CHECK(press(true) == K::Prev);   // Maj : en arrière
    md::CycleKeyEvent e;
    e.alt = false;
    CHECK(md::cycleKeyAction(e) == K::Pass);   // sans Alt : le caractère, tapé dans l'app
    e.alt = e.ctrl = true;
    CHECK(md::cycleKeyAction(e) == K::Pass);   // Alt Gr (Ctrl+Alt) : jamais pris
    e.ctrl = false;
    e.win = true;
    CHECK(md::cycleKeyAction(e) == K::Pass);   // avec ⊞ : pas notre raccourci
    e.win = false;
    e.injected = true;
    CHECK(md::cycleKeyAction(e) == K::Pass);   // frappe simulée (une app qui tape un texte)
}

TEST_CASE(window_cycle_key_release_and_repeat) {
    using K = md::CycleKey;
    md::CycleKeyEvent e;
    e.alt = e.repeat = e.taken = true;
    CHECK(md::cycleKeyAction(e) == K::Swallow);   // appui tenu : une seule fenêtre par appui
    e.repeat = false;
    e.down = false;
    CHECK(md::cycleKeyAction(e) == K::Swallow);   // relâchement d'un appui pris
    e.alt = false;
    CHECK(md::cycleKeyAction(e) == K::Swallow);   // même si Alt a été lâché avant
    e.taken = false;
    CHECK(md::cycleKeyAction(e) == K::Pass);      // relâchement d'une frappe laissée à l'app
}

TEST_CASE(window_cycle_key_steps_back_in_the_switcher) {   // ⌘` pendant ⌘⇥ : une case en arrière
    using K = md::CycleKey;
    md::CycleKeyEvent e;
    e.alt = e.session = true;
    CHECK(md::cycleKeyAction(e) == K::SwitcherBack);
    e.shift = true;
    CHECK(md::cycleKeyAction(e) == K::SwitcherNext);
}

TEST_CASE(window_cycle_target_visits_every_window_in_order) {   // ordre stable : chaque appui en montre une autre
    const std::vector<bool> all = {true, true, true};
    CHECK(md::cycleTarget(all, 0, false) == at(1));
    CHECK(md::cycleTarget(all, 1, false) == at(2));
    CHECK(md::cycleTarget(all, 2, false) == at(0));   // en boucle
    CHECK(md::cycleTarget(all, 0, true) == at(2));    // Maj : la précédente
    CHECK(md::cycleTarget(all, 2, true) == at(1));
    const std::vector<bool> minimized = {true, false, true};   // la deuxième réduite : sautée, comme sur macOS
    CHECK(md::cycleTarget(minimized, 0, false) == at(2));
    CHECK(md::cycleTarget(minimized, 2, false) == at(0));
    CHECK(md::cycleTarget(minimized, 1, false) == at(2));   // depuis une fenêtre qui ne compte pas
    CHECK(md::cycleTarget(all, std::nullopt, false) == at(0));   // premier plan sur une autre fenêtre : la première
    CHECK(md::cycleTarget({false, true, true}, std::nullopt, true) == at(2));   // ou la dernière qui compte
    CHECK(md::cycleTarget({false, true, true}, std::nullopt, false) == at(1));
    CHECK(md::cycleTarget(all, 7, false) == at(0));   // hors liste : comme absente
}

TEST_CASE(window_cycle_target_needs_another_window) {   // une seule fenêtre : rien ne bouge
    CHECK(!md::cycleTarget({true}, 0, false));
    CHECK(!md::cycleTarget({true, false}, 0, true));
    CHECK(!md::cycleTarget({}, std::nullopt, false));
    CHECK(!md::cycleTarget({false, false}, std::nullopt, false));
}

TEST_CASE(window_cycle_hotkey_setting) {
    CHECK(md::parseWindowCycleHotkey(L"alt+backtick"));
    CHECK(md::parseWindowCycleHotkey(L"Alt + Backtick"));
    CHECK(!md::parseWindowCycleHotkey(L"off"));
    CHECK(!md::parseWindowCycleHotkey(L"alt+tab"));
    CHECK(md::settingsFromJson(*md::json::parse("{}")).windowCycleHotkey == L"alt+backtick");
    CHECK(md::settingsFromJson(*md::json::parse("{\"windowCycleHotkey\":\"OFF\"}")).windowCycleHotkey == L"off");
    CHECK(md::settingsFromJson(*md::json::parse("{\"windowCycleHotkey\":\"ctrl+x\"}")).windowCycleHotkey == L"alt+backtick");
    md::Settings s;
    s.windowCycleHotkey = L"off";
    CHECK(md::settingsFromJson(md::settingsToJson(s)).windowCycleHotkey == L"off");
}
