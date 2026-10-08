// Touche ⌘ (option) : Alt de gauche joue le rôle de ⌘ ; Alt Gr n'est jamais touché ; Alt garde son rôle avec les autres
// touches (Alt+Tab, Alt+F4, Alt+Entrée).
#include <windows.h>

#include "minitest.h"
#include "../src/interact/command_key.h"

using md::CommandAction;
using md::CommandKeys;

TEST_CASE(command_key_alt_c_becomes_ctrl_c) {
    CommandKeys k;
    CHECK(k.onKey(VK_LMENU, true, false).kind == CommandAction::Kind::Swallow);   // Alt gardé de côté
    const CommandAction c = k.onKey('C', true, false);
    CHECK(c.kind == CommandAction::Kind::Send && c.chord.vk == 'C' && c.chord.ctrl && !c.chord.alt);
    CHECK(k.onKey('C', false, false).kind == CommandAction::Kind::Swallow);
    CHECK(k.onKey(VK_LMENU, false, false).kind == CommandAction::Kind::Swallow);   // jamais vu par l'app : pas de menu
}

TEST_CASE(command_key_shift_kept_and_special_keys) {
    CommandKeys k;
    k.onKey(VK_LMENU, true, false);
    CommandAction z = k.onKey('Z', true, true);   // ⌘⇧Z : rétablir
    CHECK(z.kind == CommandAction::Kind::Send && z.chord.ctrl && z.chord.shift);
    CommandAction q = k.onKey('Q', true, false);   // ⌘Q : quitter (Alt+F4, l'app demande d'enregistrer)
    CHECK(q.kind == CommandAction::Kind::Send && q.chord.vk == VK_F4 && q.chord.alt && !q.chord.ctrl);
    CommandAction left = k.onKey(VK_LEFT, true, true);   // ⌘⇧← : sélection jusqu'au début de la ligne
    CHECK(left.kind == CommandAction::Kind::Send && left.chord.vk == VK_HOME && left.chord.shift && !left.chord.ctrl);
    CommandAction up = k.onKey(VK_UP, true, false);   // ⌘↑ : début du document
    CHECK(up.kind == CommandAction::Kind::Send && up.chord.vk == VK_HOME && up.chord.ctrl);
}

TEST_CASE(command_key_other_keys_get_a_real_alt) {
    CommandKeys k;
    k.onKey(VK_LMENU, true, false);
    // Alt+Tab, Alt+F4, Alt+Entrée : Alt est rendu à Windows avant la touche.
    CHECK(k.onKey(VK_TAB, true, false).kind == CommandAction::Kind::AltThenPass);
    CHECK(k.onKey(VK_TAB, false, false).kind == CommandAction::Kind::Pass);
    CHECK(k.onKey(VK_TAB, true, false).kind == CommandAction::Kind::Pass);   // Alt déjà rendu
    CHECK(k.onKey('C', true, false).kind == CommandAction::Kind::Pass);      // Alt réel : plus de ⌘ jusqu'au relâchement
    CHECK(k.onKey(VK_LMENU, false, false).kind == CommandAction::Kind::Pass);   // relâchement rendu aussi
    CHECK(k.onKey(VK_LMENU, true, false).kind == CommandAction::Kind::Swallow);  // appui suivant : ⌘ de nouveau
}

TEST_CASE(command_key_altgr_and_untouched_cases) {
    CommandKeys k;
    CHECK(k.onKey(VK_RMENU, true, false).kind == CommandAction::Kind::Pass);   // Alt Gr (@, #, {) : jamais touché
    CHECK(k.onKey('2', true, false).kind == CommandAction::Kind::Pass);
    CHECK(k.onKey(VK_RMENU, false, false).kind == CommandAction::Kind::Pass);
    CHECK(k.onKey('C', true, false).kind == CommandAction::Kind::Pass);        // sans Alt : rien
    CommandKeys held;
    held.onKey(VK_LMENU, true, false);
    CHECK(held.onKey(VK_LMENU, true, false).kind == CommandAction::Kind::Swallow);   // répétition d'Alt
    CHECK(held.onKey(VK_LSHIFT, true, false).kind == CommandAction::Kind::Pass);     // Maj passe (⌘⇧…)
    CHECK(held.onKey('C', true, true).kind == CommandAction::Kind::Send);
}

TEST_CASE(command_key_finder_shortcuts_in_explorer) {
    CommandKeys k;
    k.onKey(VK_LMENU, true, false);
    const CommandAction up = k.onKey(VK_UP, true, false, true);   // ⌘↑ : dossier parent (Alt+↑ de l'Explorateur)
    CHECK(up.kind == CommandAction::Kind::Send && up.chord.vk == VK_UP && up.chord.alt && !up.chord.ctrl);
    const CommandAction down = k.onKey(VK_DOWN, true, false, true);   // ⌘↓ : ouvrir
    CHECK(down.kind == CommandAction::Kind::Send && down.chord.vk == VK_RETURN && !down.chord.alt);
    const CommandAction del = k.onKey(VK_BACK, true, false, true);    // ⌘⌫ : à la Corbeille
    CHECK(del.kind == CommandAction::Kind::Send && del.chord.vk == VK_DELETE);
    CHECK(k.onKey(VK_BACK, true, false, false).kind == CommandAction::Kind::AltThenPass);   // ailleurs : Alt+⌫ normal
}
