// Coup d'œil : fichiers sélectionnés dans la fenêtre de l'Explorateur au premier plan (onglet visible) ou sur le bureau.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace md {

// Chemins des éléments sélectionnés, dans l'ordre de la vue ; vide si rien (ou si la fenêtre n'est pas une vue Shell).
// Appelé sur un fil COM en STA.
std::vector<std::wstring> shellSelection(HWND foreground);

} // namespace md
