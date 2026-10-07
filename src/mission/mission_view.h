// Mission Control, façon macOS : les fenêtres du bureau courant s'écartent et se rangent sans se chevaucher, en
// miniatures vivantes, sur chaque écran. API modale : un clic sur une fenêtre la renvoie ; Échap, un clic dans le
// vide ou closeOpen() ferment sans choix.
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "../core/bgra_image.h"
#include "../popup/menu_window.h"
#include "mission_layout.h"

namespace md {

class MissionView {
public:
    struct Window {
        HWND hwnd = nullptr;
        std::wstring title;
    };
    struct Request {
        std::vector<Window> windows;   // fenêtres visibles, non réduites, du bureau courant
    };
    // Fenêtre choisie ; nullopt : fermée sans choix, ou ouverture impossible.
    static std::optional<HWND> track(const MenuWindow::Env& env, const Request& request);
    static bool isOpen();
    static void closeOpen();   // second appui : animation de retour puis fermeture
};

// Même dessin hors écran (aucune fenêtre) : fond Tahoe, rectangles colorés à la place des miniatures ; hover : indice
// de la fenêtre survolée (-1 : aucune).
BgraImage missionSnapshot(const std::vector<MissionRect>& windows, bool dark, int width, int height, int hover);

} // namespace md
