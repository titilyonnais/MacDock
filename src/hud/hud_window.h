// Pastille du volume et de la luminosité, façon macOS Tahoe : petit panneau en verre en haut à droite, sous la barre
// de menus (titre, sortie audio, pictogramme, jauge). Jamais activée ; la barre de menus la pilote (show, fondu, hide).
#pragma once
#include <windows.h>

#include <memory>

#include "../core/bgra_image.h"
#include "../popup/menu_window.h"
#include "hud_logic.h"

namespace md {

class HudWindow {
public:
    HudWindow();
    ~HudWindow();
    HudWindow(const HudWindow&) = delete;
    HudWindow& operator=(const HudWindow&) = delete;

    // Affiche la pastille à place (pixels de l'écran mon), ou met à jour celle qui est affichée ; false si impossible.
    bool show(const MenuWindow::Env& env, HMONITOR mon, const HudPlace& place, const HudContent& content);
    void setOpacity(float opacity);   // fondu (0..1)
    void hide();
    bool visible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Même dessin hors écran (aucune fenêtre) : fond Tahoe, barre de 24 pt supposée, pastille à sa place.
BgraImage hudSnapshot(const HudContent& content, bool dark, int width, int height);

} // namespace md
