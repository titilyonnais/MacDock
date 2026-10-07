// Menus contextuels en verre (façon macOS Tahoe) : panneaux DirectComposition avec Liquid Glass sur
// l'arrière-plan capturé, sous-menus, clavier, fermeture au clic extérieur. API modale, comme TrackPopupMenu.
#pragma once
#include <windows.h>
#include <d3d11.h>

#include <string>

#include "../config/metrics.h"
#include "menu_model.h"

namespace md {

class MenuWindow {
public:
    struct Env {
        HINSTANCE instance = nullptr;
        ID3D11Device* device = nullptr;   // device du Dock (nullptr : verre dépoli seulement)
        bool dark = false;
        bool glass = true;                // false : verre dépoli Direct2D (pas de capture)
        float scale = 1;                  // pixels par point
        std::wstring font;                // famille déjà résolue
        Metrics metrics;
        bool trace = false;
    };
    // Côté d'ouverture par rapport à l'ancrage : au-dessus (Dock en bas), à droite (Dock à gauche), à gauche.
    enum class Side { Above, Right, Left };
    // Ouvre le menu du côté demandé, centré sur le point d'ancrage (écran) ; renvoie l'identifiant choisi, ou 0.
    static int track(const Env& env, const MenuModel& model, POINT anchorScreen, Side side = Side::Above);
};

} // namespace md
