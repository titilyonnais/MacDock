// Écrans proposés par l'app Réglages (« Écran du Dock ») : leur nom, comme sur macOS (« DELL U2720Q »,
// « Écran intégré »), plutôt qu'un numéro.
#pragma once
#include <map>
#include <string>
#include <vector>

namespace md {

struct ScreenChoice {
    std::wstring name;          // nom du moniteur ; vide : inconnu
    int width = 0, height = 0;  // définition en pixels
    bool primary = false;
};

// Libellés dans l'ordre des écrans : nom (numéroté « (1) », « (2) » quand deux écrans portent le même), ou
// « Écran N » sans nom ; puis la définition et « (principal) ».
std::vector<std::wstring> screenLabels(const std::vector<ScreenChoice>& screens);

// Nom de chaque écran actif, par source GDI (« \\.\DISPLAY1 ») : nom du moniteur (EDID), « Écran intégré » pour la
// dalle d'un portable. Lecture seule de la configuration d'affichage.
std::map<std::wstring, std::wstring> monitorNames();

} // namespace md
