// Masquage automatique du Dock et retrait en plein écran (logique pure, temps absolu en secondes).
#pragma once
#include <windows.h>

namespace md {

struct VisibilityInputs {
    bool autohide = false;
    bool fullscreen = false;     // la fenêtre au premier plan couvre l'écran du Dock
    bool cursorAtEdge = false;   // curseur au bord inférieur de l'écran
    bool cursorInDock = false;
    bool menuOpen = false;
    bool dragging = false;
};

struct VisibilityTimings {
    double showDelay = 0;        // avant l'apparition (macOS : autohide-delay)
    double leaveDelay = 0.5;     // le Dock reste un instant après le départ du curseur
    double showSeconds = 0.45;
    double hideSeconds = 0.45;
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
    bool immediate_ = false;    // plein écran : pas de délai
};

// La fenêtre couvre-t-elle tout l'écran (tolérance de 1 px) ?
bool coversMonitor(const RECT& window, const RECT& monitor);
// Plein écran : couvre l'écran sans être une fenêtre maximisée à barre de titre (qui couvre tout l'écran
// quand la barre Windows est masquée et que le Dock ne réserve rien).
bool isFullscreenWindow(const RECT& window, const RECT& monitor, bool zoomed, bool hasCaption);

} // namespace md
