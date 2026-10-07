// Pile ouverte, façon macOS : éventail d'icônes en arc au-dessus de la pile (nom à gauche de chaque icône),
// ou grille dans un panneau de verre (titre, défilement à la molette, « Ouvrir dans l'Explorateur »).
// API modale, comme MenuWindow : un clic hors de la pile ou Échap la ferme sans choix.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "../icons/icon_provider.h"
#include "../stack/stack_model.h"
#include "menu_window.h"

namespace md {

class StackWindow {
public:
    using Env = MenuWindow::Env;
    struct Request {
        std::wstring folder, title;
        std::vector<StackItem> items;                      // déjà triés (les plus récents d'abord, etc.)
        StackView view = StackView::Fan;                   // Fan ou Grid (déjà résolue)
        MenuWindow::Side side = MenuWindow::Side::Above;   // côté d'ouverture (Dock en bas, à gauche, à droite)
        POINT iconCenter{};                                // écran : centre de l'icône de la pile
        LONG dockEdge = 0;                                 // écran : bord extérieur du Dock, côté ouverture
        double tile = 48;                                  // taille des cases du Dock (points)
        IconProvider* icons = nullptr;
    };
    // Chemin choisi (un élément, ou le dossier pour « Ouvrir dans l'Explorateur ») ; vide si fermée sans choix.
    static std::wstring track(const Env& env, const Request& request);
};

} // namespace md
