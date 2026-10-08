// Feux tricolores (logique pure) : quelles fenêtres en ont, où les poser, ce qu'un clic y fait, et leur image.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "menubar_settings.h"

namespace md {

struct LightsWindowInfo {
    LONG style = 0, exStyle = 0;
    UINT classStyle = 0;
    std::wstring className;
    RECT frame{};    // cadre visible (DWMWA_EXTENDED_FRAME_BOUNDS), pixels écran
    RECT client{};   // zone client, pixels écran
    bool zoomed = false, iconic = false, ownProcess = false;
    LONG captionBottom = 0;   // bas de la barre de titre (GetTitleBarInfo) ; 0 = inconnu (on prend client.top)
    bool elevated = false;    // processus d'intégrité plus élevée : nos messages seraient refusés (UIPI)
};

// Fenêtres à barre de titre (ni outil, ni shell, ni élevées…) ; leur place est décidée ensuite (leftCaptionFree,
// captionButtons).
bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT dpi);

struct LightsLayout {
    RECT window{};       // le calque (pixels écran)
    RECT circles[3]{};   // fermer, réduire, zoom (pixels écran)
    double radius = 0;   // pixels
    RECT patch{};        // fond de la couleur de la barre de titre
    bool lights = true;  // false : simple cache (boutons de Windows recouverts)
    bool fade = true;    // fondu du fond sur la fin à droite (pastilles posées à gauche)
    LONG topGap = 0;     // rangées du haut laissées transparentes : le bord de la fenêtre reste redimensionnable
};

// Code WM_NCHITTEST de la fenêtre en un point écran (HTNOWHERE si elle ne répond pas).
using HitProbe = std::function<LRESULT(POINT)>;
// Boutons réduire / agrandir / fermer de Windows ou de l'app, en pixels écran ; vide si aucun. dwmBounds :
// DWMWA_CAPTION_BUTTON_BOUNDS (repère de window = GetWindowRect) ; vide (l'app les dessine : Chromium, Electron…),
// la fenêtre est sondée depuis son bord droit.
RECT captionButtons(const RECT& window, const RECT& frame, const RECT& dwmBounds, UINT dpi, const HitProbe& hit);
// La place des pastilles à gauche ne contient que de la légende (ou l'icône système) sur toute leur hauteur :
// pas d'onglets ni de menus de l'app dessous. titleBottom : bas de la barre de titre (pixels écran).
bool leftCaptionFree(const RECT& frame, LONG titleBottom, UINT dpi, const HitProbe& hit);
// Partie visible du cadre : une fenêtre agrandie peut déclarer un cadre qui déborde sous la barre de menus.
RECT visibleFrame(const RECT& frame, const RECT& work, bool zoomed);
// Pastilles posées à la place des boutons de Windows (pas de place à gauche), qu'elles recouvrent. Le haut reste
// transparent pour redimensionner par le bord, sauf fenêtre agrandie (les vrais boutons y seraient atteignables).
LightsLayout lightsOverButtons(const RECT& buttons, UINT dpi, bool zoomed = false);
// Cache des boutons de Windows (pastilles à gauche).
LightsLayout buttonsCover(const RECT& buttons, UINT dpi, bool zoomed = false);
LightsLayout lightsLayout(const RECT& frame, const RECT& client, UINT dpi);
// Même chose, la barre de titre s'arrêtant à captionBottom quand il est connu (une barre de menus classique
// sous le titre n'est pas couverte).
LightsLayout lightsLayoutFor(const LightsWindowInfo& w, UINT dpi);

// Souris sur le calque : appui sur une pastille disponible, déplacement de la fenêtre depuis le fond, zoom par
// double-clic sur le fond ; un double-clic sur une pastille ne fait rien (pas de seconde commande).
enum class LightsMouse { None, Press, Drag, Zoom };
LightsMouse lightsMouse(bool doubleClick, int hit, const bool enabled[3]);
UINT captionDoubleClick(bool maximizable, bool zoomed);   // 0 : rien

int hitLight(const LightsLayout& l, POINT screen);   // 0 fermer, 1 réduire, 2 zoom, -1 ailleurs
UINT lightCommand(int light, bool zoomed);            // SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE ou SC_RESTORE
// Couleur la plus fréquente (à 8 niveaux près par canal), 0xRRGGBB ; 0 sans échantillon.
std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples);

struct LightsState {
    bool hover = false;                          // symboles ×, −, + (survol du groupe)
    int pressed = -1;                            // pastille sous le doigt : plus sombre, un peu enfoncée
    int bouncing = -1;                           // pastille relâchée : petit rebond élastique (rayon × bounce)
    double bounce = 1;
    bool enabled[3] = {true, true, true};        // indisponible : gris, sans action
    bool dark = false;                           // thème de la barre de titre (gris des pastilles indisponibles)
    std::uint32_t patchColor = 0xF3F3F3;         // 0xRRGGBB, couleur de la barre de titre
};
// Image BGRA prémultipliée du calque (taille de l.window) ; scale = dpi / 96.
std::vector<std::uint8_t> renderLights(const LightsLayout& l, const LightsState& s, double scale);
// Planche hors écran (BGRA opaque) : thème clair puis sombre ; normal, survol, indisponible ; à 200 %.
std::vector<std::uint8_t> lightsSheet(UINT& w, UINT& h);

} // namespace md
