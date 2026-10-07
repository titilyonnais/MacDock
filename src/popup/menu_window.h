// Menus contextuels en verre (façon macOS Tahoe) : panneaux DirectComposition avec Liquid Glass sur
// l'arrière-plan capturé, sous-menus, clavier, fermeture au clic extérieur. API modale, comme TrackPopupMenu.
#pragma once
#include <windows.h>
#include <d3d11.h>

#include <string>
#include <vector>

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
    // Côté d'ouverture par rapport à l'ancrage : au-dessus (Dock en bas), à droite (Dock à gauche), à gauche,
    // ou en dessous (barre de menus : bord gauche du panneau sur l'ancrage).
    enum class Side { Above, Right, Left, Below };
    // Barre de menus : titres voisins (écran) et titre ouvert. Survoler un autre titre, ou les flèches gauche et
    // droite au premier niveau, ferment le menu : track renvoie alors menuSwitchResult(k).
    struct BarLink {
        std::vector<RECT> titles;
        int current = -1;
    };
    // Ouvre le menu du côté demandé, centré sur le point d'ancrage (écran) ; renvoie l'identifiant choisi, 0 si
    // rien n'est choisi, ou menuSwitchResult(k) (barre de menus).
    static int track(const Env& env, const MenuModel& model, POINT anchorScreen, Side side = Side::Above,
                     const BarLink* bar = nullptr);
};

} // namespace md
