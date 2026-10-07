// Panneau du sélecteur d'apps, façon Cmd+Tab : rangée d'icônes en verre au centre de l'écran, nom de l'app
// sélectionnée dessous. Non modal et jamais activé : le Dock le pilote (show, select, hide) pendant qu'Alt est enfoncé.
#pragma once
#include <windows.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/bgra_image.h"
#include "../icons/icon_provider.h"
#include "../popup/menu_window.h"
#include "switcher_logic.h"

namespace md {

class SwitcherWindow {
public:
    struct Entry {
        std::wstring name;
        IconProvider::ImagePtr icon;
    };
    SwitcherWindow();
    ~SwitcherWindow();
    // Affiche le panneau sur l'écran mon ; false si impossible (rien n'est affiché).
    bool show(const MenuWindow::Env& env, HMONITOR mon, std::vector<Entry> entries, std::size_t selected);
    void select(std::size_t index);
    void remove(std::size_t index);   // app fermée (Q) : retirée de la rangée
    void hide();
    bool visible() const;
    std::function<void(std::size_t)> onClick;   // clic sur une icône

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Même dessin hors écran (aucune fenêtre) : fond Tahoe, panneau dépoli, cases de couleur à la place des icônes.
BgraImage switcherSnapshot(const std::vector<std::wstring>& names, std::size_t selected, bool dark, int width, int height);

} // namespace md
