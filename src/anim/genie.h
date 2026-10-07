// Effet génie et effet échelle (logique pure) : la fenêtre réduite est découpée en bandes, chacune posée à
// l'écran ; à l'écran, une miniature DWM par bande.
#pragma once
#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

#include "../config/settings.h"

namespace md {

struct GenieSlice {
    RECT src;   // pixels de la fenêtre source
    RECT dst;   // pixels écran
};

// t = 0 : la fenêtre à sa place (from) ; t = 1 : dans la case du Dock (to). Bandes perpendiculaires à la
// direction du Dock (lignes de la source en bas, colonnes à gauche et à droite), de la plus éloignée à la plus
// proche du Dock, jointives. Vide pour l'effet Windows ou une source vide.
std::vector<GenieSlice> minimizeFrame(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t,
                                      int slices = 48);

// Sommet du maillage du génie : position écran (pixels, non arrondie) et coordonnées de texture dans [0, 1].
struct GenieVertex {
    float x, y, u, v;
};
// Maillage de la même animation que minimizeFrame : rows + 1 lignes de deux sommets (côté u0 puis côté u1 du repère
// du Dock), de la plus éloignée à la plus proche du Dock ; une rangée pour l'effet échelle ; vide pour l'effet Windows
// ou une source vide. Sous le pixel : bords courbes sans marches.
std::vector<GenieVertex> genieMesh(MinimizeEffect e, SIZE src, const RECT& from, const RECT& to, DockPosition edge, double t,
                                   int rows);

// Durée en secondes (Maj enfoncée : ralenti × 8) ; 0 pour l'effet Windows.
double minimizeDuration(MinimizeEffect e, bool slow);
// Départ d'une animation dont le rendu GPU est attendu : la fenêtre reste immobile à sa place (les bandes, à
// l'instant de départ, la reproduisent exactement) jusqu'à la première image GPU, pour que tout le mouvement soit
// rendu par le GPU, sans raccord visible ; au-delà de 150 ms, l'animation part sur les bandes.
enum class GenieWait { Hold, Go, GoStrips };
GenieWait genieWaitStep(bool gpuReady, double waited);
// Bandes à tenir (have déjà posées, full au plein) : une seule au départ et tant que la fenêtre attend le GPU,
// immobile (pas encore déformée : affichée sans délai) ; toutes si elle bouge sans le GPU.
int genieStripTarget(int full, bool waiting, int have);
// Bandes du repli par miniatures DWM (un appel à DWM par bande et par image) : une toutes les 4 px, 16 à 128.
int genieSliceCount(long extent);

// Rectangle écran d'une fenêtre réduite avant sa réduction. rcNormalPosition est en coordonnées de la zone de
// travail (sauf fenêtre outil) ; réduite depuis l'état agrandi : la zone de travail, débordée des bordures
// invisibles (src plus grande, centrée).
RECT restoredRect(const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor, bool toolWindow, SIZE src);

// Partie visible d'une fenêtre dont on connaît le rectangle Windows (bordures de redimensionnement invisibles
// comprises : ~11 px à gauche, à droite et en bas, 0 en haut) et la taille de sa miniature DWM (partie visible
// seule) : centrée en largeur, calée en haut. Rectangle inchangé s'il a déjà cette taille ou en diffère trop.
RECT genieVisibleRect(const RECT& window, SIZE thumb);
// Réduction annoncée par l'appui sur « réduire » : confirmée si le bouton est relâché sur place (4 px au plus). Pas de
// test de la fenêtre sous le pointeur : l'app peut l'avoir déjà retirée.
bool genieMinimizeConfirmed(POINT down, POINT up);
// Bouton « réduire » sous le point (écran) d'après la zone des boutons de titre de DWM (DWMWA_CAPTION_BUTTON_BOUNDS,
// repère de la fenêtre) : le premier tiers (réduire, agrandir, fermer). Zone vide : boutons dessinés par l'app.
bool genieOnMinimizeButton(POINT pt, const RECT& window, const RECT& buttonBounds, bool hasMinimizeBox);
// Cadre de la fenêtre des bandes : l'écran de la fenêtre et celui du Dock (union des deux rectangles d'écran).
RECT genieHostBox(const RECT& windowMonitor, const RECT& dockMonitor);
// Surface GPU d'une animation de from vers to (pixels écran, 2 px de marge pour l'anticrénelage) ; armed : surface posée
// d'avance (réduction annoncée), reprise telle quelle si elle contient l'animation.
RECT genieGpuBox(const RECT& from, const RECT& to, const RECT* armed);

// Départ d'une réduction : le dernier rectangle vu à l'écran (fenêtre ancrée, agrandie…) s'il est connu, sinon
// restoredRect.
RECT genieStartRect(const std::optional<RECT>& lastSeen, const WINDOWPLACEMENT& wp, const RECT& work, const RECT& monitor,
                    bool toolWindow, SIZE src);

// Animation en cours, vue par le Dock.
struct GenieRun {
    bool active = false;
    std::uint64_t source = 0;
    bool restoring = false;
    bool settling = false;   // ouverture finie : la fenêtre, déjà restaurée, prend la place de la dernière image
};
enum class GenieReact { Nothing, Start, Cancel };
// Fenêtre réduite (minimized) ou restaurée. live : réduction vue à l'instant, pas une fenêtre découverte déjà
// réduite (lancement du Dock, redémarrage de l'Explorateur). Restaurée ailleurs pendant son animation : annulée.
GenieReact genieOnMinimize(const GenieRun& run, std::uint64_t window, bool minimized, bool live);
// Une animation va en remplacer une autre : une restauration interrompue doit quand même aboutir.
bool genieMustRestoreFirst(const GenieRun& run);

} // namespace md
