// Apparence macOS des fenêtres des autres apps, par les attributs DWM (aucune injection) : coins arrondis, pas de
// liseré de couleur d'accentuation, et barre de titre gris clair (ou gris très foncé) quand Windows la dessine.
// Tout est rendu aux valeurs par défaut de Windows à la fermeture de la barre de menus.
#pragma once
#include <windows.h>

#include <optional>
#include <set>

#include "traffic_lights.h"

namespace md {

struct WindowLook {
    bool round = true;        // DWMWCP_ROUND
    bool noBorder = true;     // DWMWA_COLOR_NONE : pas de liseré d'accentuation
    bool caption = false;     // couleurs de la barre de titre (seulement si Windows la dessine)
    COLORREF captionColor = 0, textColor = 0;
};

// Fenêtres à barre de titre d'une autre app (mêmes règles que les pastilles) ; nullopt sinon.
std::optional<WindowLook> macWindowLook(const LightsWindowInfo& w, bool dark, UINT dpi);

class WindowStyler {
public:
    ~WindowStyler() { restoreAll(); }
    // Applique l'apparence à une fenêtre (déjà faite : rien, sauf si le mode clair ou sombre a changé).
    void apply(HWND h, const LightsWindowInfo& info, bool dark, UINT dpi);
    void applyAll(bool dark);   // fenêtres visibles de premier niveau (démarrage, changement clair/sombre)
    void restoreAll();          // valeurs par défaut de Windows sur toutes les fenêtres touchées
    void setEnabled(bool on, bool dark);

private:
    struct Touched {
        HWND h;
        bool dark;
        bool operator<(const Touched& o) const { return h < o.h; }
    };
    std::set<Touched> touched_;
    bool enabled_ = true;
};

} // namespace md
