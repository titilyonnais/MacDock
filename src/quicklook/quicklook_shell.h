// Coup d'œil : fichiers sélectionnés dans la fenêtre de l'Explorateur au premier plan (onglet visible) ou sur le bureau.
#pragma once
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

namespace md {

// Chemins des éléments sélectionnés, dans l'ordre de la vue ; vide si rien (ou si la fenêtre n'est pas une vue Shell).
// Appelé sur un fil COM en STA.
std::vector<std::wstring> shellSelection(HWND foreground);

// Sélection d'une fenêtre donnée, suivie pendant l'aperçu : la vue est retrouvée une seule fois, puis seul le premier
// élément sélectionné est lu (Ctrl+A dans 10 000 fichiers ne coûte rien de plus).
class ShellSelectionWatch {
public:
    bool attach(HWND owner);   // false : pas une vue Shell
    std::wstring first();      // premier élément sélectionné ; vide si aucun
    void reset();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace md
