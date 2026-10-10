// Filtre des événements WinEvent du suivi des fenêtres (plan 62, logique pure). Les crochets sont posés pour tout le
// système : une app bavarde (le Gestionnaire des tâches, ~100 événements par seconde sur les lignes de sa liste)
// réveillait la boucle du Dock à chaque événement. Seuls ceux d'une fenêtre (OBJID_WINDOW, CHILDID_SELF) l'intéressent.
#pragma once
#include <windows.h>

namespace md {

inline bool forwardedWinEvent(HWND hwnd, LONG idObject, LONG idChild) {
    return hwnd && idObject == OBJID_WINDOW && idChild == CHILDID_SELF;
}

} // namespace md
