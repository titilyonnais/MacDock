// Menus propres aux apps courantes (données) : titres et entrées à la manière de leurs menus sur macOS, chaque
// entrée envoyant le vrai raccourci Windows de l'app ou ouvrant une adresse. Les apps sont repérées par le nom de
// leur exécutable. Raccourcis limités aux touches qui ne dépendent pas de la disposition du clavier (pas de « / »,
// « [ », « ` » : ailleurs qu'en QWERTY, ils tomberaient sur une autre touche).
#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace md {

struct CatalogItem {
    std::wstring text;       // vide : séparateur
    std::wstring shortcut;   // envoyé à l'app (« Ctrl+Maj+N ») et affiché
    std::wstring uri;        // à ouvrir à la place d'un raccourci (steam://…)
    std::vector<CatalogItem> submenu;
};

struct CatalogMenu {
    std::wstring title;
    std::vector<CatalogItem> items;
};

struct MenuCatalog {
    std::vector<std::wstring> exes;          // noms d'exécutables, en minuscules
    std::vector<CatalogMenu> menus;          // sans Fenêtre (ajoutée avant l'Aide) ; Aide générique si absente
    std::vector<CatalogItem> windowExtras;   // en tête du menu Fenêtre (Téléchargements…)
    std::wstring settingsShortcut = L"Ctrl+,", settingsUri;   // Réglages… du menu de l'app ; les deux vides : absent
};

const std::vector<MenuCatalog>& menuCatalog();
const MenuCatalog* menuCatalogFor(std::wstring_view exeName);   // nullptr : menus génériques

} // namespace md
