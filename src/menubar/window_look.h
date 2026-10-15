// Apparence macOS des fenêtres des autres apps, par les attributs DWM (aucune injection) : coins arrondis, pas de
// liseré de couleur d'accentuation, et barre de titre gris clair (ou gris très foncé) quand Windows la dessine.
// Tout est rendu aux valeurs par défaut de Windows à la fermeture de la barre de menus.
#pragma once
#include <windows.h>

#include <optional>
#include <set>

#include "window_info.h"

namespace md {

struct WindowLook {
    bool round = true;        // DWMWCP_ROUND
    bool noBorder = true;     // DWMWA_COLOR_NONE : pas de liseré d'accentuation
    bool caption = false;     // couleurs de la barre de titre (seulement si Windows la dessine)
    COLORREF captionColor = 0, textColor = 0;
};

// Fenêtre à barre de titre d'une autre app que MacDock habille : ni outil, ni shell, ni élevée, ni fenêtre de MacDock
// qui règle son cadre elle-même (own_lights.h), ni réduite.
bool styledWindow(const WindowInfo& w);
// Fenêtres à barre de titre d'une autre app (styledWindow) ; nullopt sinon. backdrop :
// DWMWA_SYSTEMBACKDROP_TYPE de la fenêtre (0 auto, 1 aucun ; 2 à 4 Mica ou Acrylic : sa barre est laissée telle quelle).
std::optional<WindowLook> macWindowLook(const WindowInfo& w, bool dark, UINT dpi, int backdrop = 0);
// Préférence de coins lue avant notre passage (DWMWA_WINDOW_CORNER_PREFERENCE) : carrés ou petits demandés par l'app
// sont gardés.
bool shouldRoundCorners(DWORD original);
// Fenêtre à barre de titre, non agrandie, qui couvre son écran (plein écran de la pastille verte, plan 72) : coins
// carrés comme sur macOS ; Windows 11 et le mod de coins arrondissent une fenêtre non agrandie.
bool squareInFullscreen(const WindowInfo& w);
// Petits coins (DWMWCP_ROUNDSMALL) pour une petite fenêtre : taille fixe (Exécuter, alertes) ou plus petit côté sous
// 300 pt. Un grand rayon y rend mal ; macOS arrondit lui aussi moins les fenêtres sans barre d'outils (plan 74).
// Seulement si le mod de coins de Windhawk leur donne au moins 8 pt (`modSmallPt`) : sans lui, les petits coins de
// Windows (4 pt) seraient moins arrondis que les 8 pt ordinaires.
bool useSmallCorners(const WindowInfo& w, UINT dpi, double modSmallPt);
// Petit rayon du mod « Custom Window Corner Radius » de Windhawk (registre, lecture seule), en points ; 0 s'il est
// absent ou coupé.
double cornerModSmallRadius();
// Mode des applications (AppsUseLightTheme), et non celui de la barre des tâches.
bool appsDarkMode();

class WindowStyler {
public:
    ~WindowStyler() { restoreAll(); }
    // Applique l'apparence à une fenêtre (déjà faite : rien, sauf si le mode clair ou sombre a changé).
    void apply(HWND h, const WindowInfo& info, bool dark, UINT dpi);
    void applyAll(bool dark);   // fenêtres visibles de premier niveau (démarrage, changement clair/sombre)
    void restoreAll();          // valeurs par défaut de Windows sur toutes les fenêtres touchées
    void setEnabled(bool on, bool dark);

private:
    struct Touched {
        HWND h;
        bool dark = false;
        DWORD pid = 0;          // un HWND réutilisé par une autre fenêtre n'est pas la même
        DWORD corner = 0;       // préférence d'origine, rendue à la fermeture
        bool rounded = false;   // nous avons changé les coins
        bool squared = false;   // coins carrés le temps du plein écran (squareInFullscreen)
        bool smallCorners = false;   // petits coins (useSmallCorners)
        bool captioned = false; // nous avons coloré la barre de titre
        DWORD backdrop = 0;     // fond de la fenêtre lors de notre passage (Mica arrivé depuis : barre rendue)
        bool operator<(const Touched& o) const { return h < o.h; }
    };
    void restore(const Touched& t);
    std::set<Touched> touched_;
    bool enabled_ = false;   // activé par setEnabled au démarrage, d'après le réglage
};

} // namespace md
