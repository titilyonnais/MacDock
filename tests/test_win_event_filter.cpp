#include <windows.h>

#include "minitest.h"
#include "../src/tracker/win_event_filter.h"

TEST_CASE(win_event_filter_keeps_only_windows) {
    // Plan 62 : le Gestionnaire des tâches émettait des centaines d'événements d'objets (DESTROY, NAMECHANGE sur les
    // lignes de sa liste) ; chacun réveillait la boucle du Dock. Seuls ceux d'une fenêtre lui sont transmis.
    const HWND w = reinterpret_cast<HWND>(static_cast<LONG_PTR>(0x1234));
    CHECK(md::forwardedWinEvent(w, OBJID_WINDOW, CHILDID_SELF));
    CHECK(!md::forwardedWinEvent(w, OBJID_CLIENT, CHILDID_SELF));
    CHECK(!md::forwardedWinEvent(w, OBJID_WINDOW, 3));       // élément d'une liste
    CHECK(!md::forwardedWinEvent(w, 42, CHILDID_SELF));       // objet propre à l'app (UI Automation)
    CHECK(!md::forwardedWinEvent(nullptr, OBJID_WINDOW, CHILDID_SELF));
    CHECK(!md::forwardedWinEvent(nullptr, OBJID_CURSOR, CHILDID_SELF));
}
