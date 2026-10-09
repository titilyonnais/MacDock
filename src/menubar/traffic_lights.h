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
    RECT monitor{};           // écran de la fenêtre (rcMonitor) ; vide = inconnu
};

// Fenêtres à barre de titre (ni outil, ni shell, ni élevées…) ; les pastilles prennent ensuite la place de leurs
// boutons réduire / agrandir / fermer (captionButtons), qu'il y en ait de Windows ou dessinés par l'app.
bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT dpi);
// Un calque pour cette fenêtre : éligible et visible (réduite compte comme visible). Une fenêtre masquée qui passe au
// premier plan (menu de son icône de notification) n'en reçoit pas : aucun HIDE ne viendrait le retirer.
bool lightsLayerWanted(const LightsWindowInfo& w, LightsMode mode, UINT dpi);

struct LightsLayout {
    RECT window{};       // le calque (pixels écran), sur les boutons de la fenêtre
    RECT circles[3]{};   // fermer, réduire, zoom (pixels écran)
    double radius = 0;   // pixels
    RECT patch{};        // fond de la couleur de la barre de titre, qui cache les boutons
    LONG topGap = 0;     // rangées du haut laissées transparentes : le bord de la fenêtre reste redimensionnable
};

// Code WM_NCHITTEST de la fenêtre en un point écran (HTNOWHERE si elle ne répond pas).
using HitProbe = std::function<LRESULT(POINT)>;
// Boutons réduire / agrandir / fermer de Windows ou de l'app, en pixels écran ; vide si aucun. dwmBounds :
// DWMWA_CAPTION_BUTTON_BOUNDS (repère de window = GetWindowRect) ; vide (l'app les dessine : Chromium, Electron…),
// la fenêtre est sondée depuis son bord droit.
RECT captionButtons(const RECT& window, const RECT& frame, const RECT& dwmBounds, UINT dpi, const HitProbe& hit);
// Partie visible du cadre : une fenêtre agrandie peut déclarer un cadre qui déborde sous la barre de menus.
RECT visibleFrame(const RECT& frame, const RECT& work, bool zoomed);
// Pastilles posées à la place des boutons de la fenêtre, qu'elles recouvrent : rien du contenu de l'app n'est caché,
// dans toutes les apps (elles ont toutes ces boutons en haut à droite). Le haut reste transparent pour redimensionner
// par le bord, sauf fenêtre agrandie (les vrais boutons y seraient atteignables).
LightsLayout lightsOverButtons(const RECT& buttons, UINT dpi, bool zoomed = false);
// Attente avant de remontrer les pastilles quand la fenêtre est agrandie ou rendue à sa taille (`animated` : animation
// de Windows active), en millisecondes.
unsigned lightsZoomWaitMs(bool wasZoomed, bool zoomed, bool animated);
// Même attente quand la fenêtre revient d'une réduction avec l'animation de Windows ; `heldByDock` : le Dock a coupé
// cette animation (restauration par le génie), elle arrive tout de suite.
unsigned lightsRestoreWaitMs(bool animated, bool heldByDock);

// Souris sur le calque : appui sur une pastille disponible, déplacement de la fenêtre depuis le fond, zoom par
// double-clic sur le fond ; un double-clic sur une pastille ne fait rien (pas de seconde commande).
enum class LightsMouse { None, Press, Drag, Zoom };
LightsMouse lightsMouse(bool doubleClick, int hit, const bool enabled[3]);
UINT captionDoubleClick(bool maximizable, bool zoomed);   // 0 : rien

int hitLight(const LightsLayout& l, POINT screen);   // 0 fermer, 1 réduire, 2 zoom, -1 ailleurs
UINT lightCommand(int light, bool zoomed);            // SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE ou SC_RESTORE
// Couleur la plus fréquente (à 8 niveaux près par canal), 0xRRGGBB ; 0 sans échantillon.
std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples);
// Points de la barre de titre lus pour cette couleur (sur une même ligne) : de `right` vers la gauche par pas de
// `step`, jusqu'à `minLeft` compris et sur `maxSpan` pixels au plus (plus loin : titre, onglets, recherche d'Office) ;
// les `want` premiers où la fenêtre elle-même est visible (`ours` : une fenêtre du dessus n'en donne pas la couleur).
// Moins de `minCount` : vide, aucun point sûr, la couleur d'avant reste.
std::vector<LONG> captionSampleXs(LONG right, LONG minLeft, LONG step, LONG maxSpan, const std::function<bool(LONG)>& ours,
                                  int want = 9, int minCount = 3);
// Fenêtre suivie masquée (EVENT_OBJECT_HIDE) : calque caché tout de suite, puis relue par la boucle du fil
// (`deferred`) : encore masquée, calque retiré (avec le crochet de déplacements de son processus, s'il était le
// dernier ; elle en retrouve un à son prochain SHOW) ; revenue, calque replacé.
enum class HiddenLayer { HideNow, Remove, Replace };
HiddenLayer onTargetHidden(bool deferred, bool visible);

struct LightsState {
    bool hover = false;                          // symboles ×, −, + (survol du groupe)
    int pressed = -1;                            // pastille sous le doigt : plus sombre
    bool enabled[3] = {true, true, true};        // indisponible : gris, sans action
    bool dark = false;                           // thème de la barre de titre (gris des pastilles indisponibles)
    bool inactive = false;                       // fenêtre inactive : grises, en couleur au survol (macOS)
    std::uint32_t patchColor = 0xF3F3F3;         // 0xRRGGBB, couleur de la barre de titre
};
// Image BGRA prémultipliée du calque (taille de l.window) ; scale = dpi / 96.
std::vector<std::uint8_t> renderLights(const LightsLayout& l, const LightsState& s, double scale);
// Planche hors écran (BGRA opaque) : thème clair puis sombre ; normal, survol, indisponible ; à 200 %.
std::vector<std::uint8_t> lightsSheet(UINT& w, UINT& h);

} // namespace md
