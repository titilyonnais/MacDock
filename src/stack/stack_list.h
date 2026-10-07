// Pile présentée « en liste » : menu en verre de ses éléments, sous-dossiers en sous-menus (logique pure).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "../popup/menu_model.h"
#include "stack_model.h"

namespace md {

constexpr int kStackListBase = 5000;   // identifiants des entrées de la liste : kStackListBase + index dans paths

// Entrées : éléments (dossier → sous-menu de son contenu, un seul niveau, kGridMaxItems au plus ; dossier vide
// → entrée simple), séparateur, « Ouvrir dans l'Explorateur ». paths[id - kStackListBase] = chemin à ouvrir.
// maxItems : entrées par niveau (les menus ne défilent pas : ce qui tient à l'écran).
MenuModel stackListMenu(const std::wstring& folder, const std::vector<StackItem>& items,
                        const std::function<std::vector<StackItem>(const std::wstring&)>& listSub,
                        std::vector<std::wstring>& paths, std::size_t maxItems = kGridMaxItems);

} // namespace md
