// Menus contextuels en verre (façon macOS Tahoe) : panneaux DirectComposition avec Liquid Glass sur
// l'arrière-plan capturé, sous-menus, clavier, fermeture au clic extérieur. API modale, comme TrackPopupMenu.
#pragma once
#include <windows.h>
#include <d3d11.h>

#include <cstdint>
#include <functional>
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
    // en dessous (barre de menus : bord gauche du panneau sur l'ancrage) ou en dessous vers la gauche (pastille verte :
    // bord droit du panneau sur l'ancrage).
    enum class Side { Above, Right, Left, Below, BelowLeft };
    // Barre de menus : titres voisins (écran) et titre ouvert. Survoler un autre titre, ou les flèches gauche et
    // droite au premier niveau, ferment le menu : track renvoie alors menuSwitchResult(k).
    struct BarLink {
        std::vector<RECT> titles;
        int current = -1;
    };
    // Lignes enrichies (curseurs, interrupteurs, tuiles, média) : rappels sur le fil du menu, qui reste ouvert.
    struct Live {
        std::function<void(int id, double value)> slider;   // pendant le glisser
        std::function<void(int id, bool on)> toggle;
        std::function<bool(int id, int tile)> tile;         // true : refermer le menu (la tuile ouvre une fenêtre)
        std::function<void(int id, int button)> media;      // 0 précédent, 1 lecture/pause, 2 suivant
        std::function<bool(MenuModel&)> refresh;            // toutes les 500 ms (voir applyRefresh)
    };
    // Ouvre le menu du côté demandé, centré sur le point d'ancrage (écran) ; renvoie l'identifiant choisi, 0 si
    // rien n'est choisi, ou menuSwitchResult(k) (barre de menus). `closed` : ce qui l'a fermé (premier plan à rendre ?).
    static int track(const Env& env, const MenuModel& model, POINT anchorScreen, Side side = Side::Above,
                     const BarLink* bar = nullptr, const Live* live = nullptr, MenuClose* closed = nullptr);
    // Rendu hors écran du menu (verre dépoli, sans fenêtre ni capture) : image BGRA prémultipliée w x h pixels
    // (vérifications, --snapshot de la barre). COM doit être initialisé.
    static bool snapshot(const Env& env, const MenuModel& model, std::vector<std::uint8_t>& bgra, UINT& w, UINT& h);
    // Capture d'écran (⊞⇧3, ⊞⇧4) : le menu ouvert sur ce fil devient visible aux captures le temps de la copie. Son
    // verre ne suit plus l'écran pendant ce temps (il s'y verrait), puis reprend 150 ms après. Sans menu : rien.
    static void setCaptureVisible(bool on);
};

} // namespace md
