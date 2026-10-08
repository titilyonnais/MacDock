// Raccourcis globaux saisis librement (app Réglages) : analyse, texte des réglages, symboles macOS, réservés, conflits.
#include <windows.h>

#include "minitest.h"
#include "../src/interact/hotkey.h"
#include "../src/mission/mission_layout.h"
#include "../src/spotlight/spot_results.h"

TEST_CASE(hotkey_parses_any_combination) {
    auto a = md::parseHotkey(L"ctrl+alt+up");
    REQUIRE(a.has_value());
    CHECK_EQ(a->mods, UINT(MOD_CONTROL | MOD_ALT));
    CHECK_EQ(a->vk, UINT(VK_UP));
    auto b = md::parseHotkey(L"Ctrl + Shift + K");   // casse et espaces ignorés
    REQUIRE(b.has_value());
    CHECK_EQ(b->mods, UINT(MOD_CONTROL | MOD_SHIFT));
    CHECK_EQ(b->vk, UINT('K'));
    auto c = md::parseHotkey(L"win+alt+7");
    REQUIRE(c.has_value());
    CHECK_EQ(c->mods, UINT(MOD_WIN | MOD_ALT));
    CHECK_EQ(c->vk, UINT('7'));
    CHECK(md::parseHotkey(L"f3")->vk == VK_F3);     // une touche F seule suffit
    CHECK(md::parseHotkey(L"shift+f13")->vk == VK_F13);
    CHECK(md::parseHotkey(L"ctrl+space")->vk == VK_SPACE);
    CHECK(md::parseHotkey(L"alt+pagedown")->vk == VK_NEXT);
    CHECK(!md::parseHotkey(L"k"));            // sans modificateur : chaque frappe le déclencherait
    CHECK(!md::parseHotkey(L"ctrl+"));
    CHECK(!md::parseHotkey(L"ctrl+bizarre"));
    CHECK(!md::parseHotkey(L"ctrl+alt"));     // pas de touche
    CHECK(!md::parseHotkey(L"off"));
    CHECK(!md::parseHotkey(L""));
}

TEST_CASE(hotkey_refuses_windows_reserved) {
    for (const wchar_t* r : {L"win+l", L"ctrl+alt+delete", L"alt+tab", L"alt+f4", L"ctrl+shift+escape", L"win+space"}) {
        CHECK(!md::parseHotkey(r));
        auto raw = md::parseHotkey(r, /*allowReserved*/ true);
        REQUIRE(raw.has_value());
        CHECK(md::hotkeyReserved(*raw));
    }
    CHECK(!md::hotkeyReserved(*md::parseHotkey(L"ctrl+alt+up")));
}

TEST_CASE(hotkey_text_label_and_conflict) {
    const md::HotkeySpec mc{MOD_CONTROL | MOD_ALT, VK_UP};
    CHECK(md::hotkeyText(mc) == L"ctrl+alt+up");
    CHECK(md::hotkeyLabel(mc) == L"⌃⌥↑");
    // Ordre de macOS : ⌃ ⌥ ⇧ puis ⊞ (Windows), puis la touche.
    CHECK(md::hotkeyLabel({MOD_WIN | MOD_SHIFT | MOD_CONTROL, 'S'}) == L"⌃⇧⊞S");
    CHECK(md::hotkeyLabel({0, VK_F3}) == L"F3");
    CHECK(md::hotkeyLabel({MOD_ALT, VK_SPACE}) == L"⌥Espace");
    for (const wchar_t* t : {L"ctrl+alt+up", L"alt+space", L"ctrl+shift+k", L"alt+win+7", L"f3", L"ctrl+pagedown", L"shift+f13"}) {
        auto p = md::parseHotkey(t);
        REQUIRE(p.has_value());
        CHECK(md::hotkeyText(*p) == t);   // aller-retour exact
    }
    CHECK(md::hotkeyConflict(L"ctrl+alt+up", L"Ctrl+Alt+Up"));
    CHECK(!md::hotkeyConflict(L"ctrl+alt+up", L"ctrl+alt+down"));
    CHECK(!md::hotkeyConflict(L"off", L"off"));   // deux raccourcis coupés ne se gênent pas
}

TEST_CASE(hotkey_dock_parsers_accept_new_and_old_values) {
    // Les anciennes valeurs des réglages restent valides ; les nouvelles combinaisons sont acceptées partout.
    CHECK(md::parseSpotlightHotkey(L"alt+space")->vk == VK_SPACE);
    CHECK(md::parseSpotlightHotkey(L"ctrl+shift+space")->mods == UINT(MOD_CONTROL | MOD_SHIFT));
    CHECK(md::parseMissionHotkey(L"f3")->vk == VK_F3);
    CHECK(md::parseMissionHotkey(L"ctrl+win+m")->vk == UINT('M'));
    CHECK(md::parseAppExposeHotkey(L"ctrl+alt+down")->vk == VK_DOWN);
    CHECK(md::parseAppExposeHotkey(L"alt+shift+e")->vk == UINT('E'));
    CHECK(!md::parseSpotlightHotkey(L"off"));
    CHECK(!md::parseMissionHotkey(L"win+l"));   // réservé
}
