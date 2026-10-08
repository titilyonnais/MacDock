// Masquage automatique du Dock et retrait en plein écran (logique pure, temps absolu en secondes).
#pragma once
#include <windows.h>

#include <functional>
#include <vector>

namespace md {

struct VisibilityInputs {
    bool autohide = false;
    bool fullscreen = false;     // la plus haute fenêtre de l'écran du Dock le couvre (fullscreenOnMonitor)
    bool cursorAtEdge = false;   // curseur au bord inférieur de l'écran
    bool cursorInDock = false;
    bool menuOpen = false;
    bool dragging = false;
};

struct VisibilityTimings {
    double showDelay = 0;        // avant l'apparition (macOS : autohide-delay)
    double leaveDelay = 0.5;     // le Dock reste un instant après le départ du curseur
    double showSeconds = 0.40;
    double hideSeconds = 0.40;
};

class Visibility {
public:
    void setTimings(const VisibilityTimings& t) { t_ = t; }
    // true tant que shown() évolue (animation en cours).
    bool update(const VisibilityInputs& in, double now);
    double shown() const;                  // 0 (masqué) .. 1 (visible), adouci
    bool hidden() const { return p_ <= 0; }
    double wakeAt() const;                 // instant où la cible changera (fin d'un délai), ou -1

private:
    VisibilityTimings t_;
    double p_ = 1;              // progression linéaire
    double last_ = -1;
    bool raw_ = true;           // cible immédiate des entrées
    double rawSince_ = 0;
    bool target_ = true;        // cible effective (après délai)
    bool wasFullscreen_ = false;
    bool immediate_ = false;    // plein écran : pas de délai
};

// La fenêtre couvre-t-elle tout l'écran (tolérance de 1 px) ?
bool coversMonitor(const RECT& window, const RECT& monitor);
// Plein écran : couvre l'écran sans être une fenêtre maximisée à barre de titre (qui couvre tout l'écran
// quand la barre Windows est masquée et que le Dock ne réserve rien).
bool isFullscreenWindow(const RECT& window, const RECT& monitor, bool zoomed, bool hasCaption);

// Fenêtre de premier niveau, dans l'ordre d'affichage (du haut vers le bas), pour la détection du plein écran.
struct ZWindow {
    RECT rect{};
    bool onMonitor = false;   // sur l'écran examiné
    bool eligible = true;     // fenêtre ordinaire : visible, ni réduite, ni masquée, ni outil, ni transparente aux clics
    bool topmost = false;     // toujours au-dessus
    bool zoomed = false, caption = false;
};
// Plein écran sur un écran : sa plus haute fenêtre ordinaire le couvre, qu'elle ait le clavier ou non (une vidéo en
// plein écran le reste quand on travaille sur l'autre écran, comme sur Mac). Les fenêtres toujours au-dessus qui ne
// le couvrent pas (vignette, pense-bête) sont passées.
bool fullscreenOnMonitor(const std::vector<ZWindow>& zOrder, const RECT& monitor);
// Même chose sur les vraies fenêtres (hors celles de ce processus et du shell) : la fenêtre en plein écran, ou nullptr.
HWND fullscreenWindowOn(HMONITOR monitor, const RECT& monitorRect);

// Suit une fenêtre en plein écran : sa sortie (taille, place, masquage, fermeture) prévient tout de suite, sans attendre
// la vérification périodique. Le rappel arrive sur le fil qui a appelé watch (par sa boucle de messages).
class FullscreenWatch {
public:
    FullscreenWatch() = default;
    FullscreenWatch(const FullscreenWatch&) = delete;
    FullscreenWatch& operator=(const FullscreenWatch&) = delete;
    ~FullscreenWatch() { stop(); }
    void watch(HWND window, std::function<void()> changed);   // même fenêtre : rien ; nullptr : arrête
    void stop();

private:
    static void CALLBACK onEvent(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    HWND window_ = nullptr;
    HWINEVENTHOOK hooks_[2] = {};
    std::function<void()> changed_;
};

} // namespace md
