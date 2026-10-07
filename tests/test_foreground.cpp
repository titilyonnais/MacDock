// Passage d'une app au premier plan depuis le Dock : la frappe qui débloque le verrou de premier plan.
#include "minitest.h"
#include <windows.h>

#include "../src/shell/shell_actions.h"

TEST_CASE(foreground_unlock_never_taps_alt_alone) {
    // Alt pressé puis relâché seul ouvre la barre de menus de l'app au premier plan : elle attend alors une touche de
    // menu, « comme sélectionnée », jusqu'à un clic ailleurs. Une touche neutre doit passer pendant qu'Alt est enfoncé.
    const std::vector<INPUT> keys = md::foregroundUnlockKeys();
    REQUIRE(keys.size() == 4);
    for (const INPUT& in : keys) CHECK(in.type == INPUT_KEYBOARD);
    CHECK(keys[0].ki.wVk == VK_MENU && !(keys[0].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(keys[1].ki.wVk == 0xE8 && !(keys[1].ki.dwFlags & KEYEVENTF_KEYUP));   // touche non attribuée
    CHECK(keys[2].ki.wVk == 0xE8 && (keys[2].ki.dwFlags & KEYEVENTF_KEYUP));
    CHECK(keys[3].ki.wVk == VK_MENU && (keys[3].ki.dwFlags & KEYEVENTF_KEYUP));   // tout est relâché
}
