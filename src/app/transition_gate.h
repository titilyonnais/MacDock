// Animations de DWM d'une fenêtre (réduction, agrandissement, ouverture) coupées pour la seule fenêtre que le Dock
// anime lui-même (génie, échelle), puis rendues dès que plus rien ne la retient : l'agrandissement et la restauration
// des autres fenêtres gardent l'animation de Windows. DWMWA_TRANSITIONS_FORCEDISABLED se pose depuis un autre
// processus, mais seulement AVANT le changement d'état : posé 5 ms après, DWM joue quand même son animation (mesuré).
#pragma once
#include <windows.h>

#include <functional>
#include <map>
#include <vector>

namespace md {

struct TransitionApi {
    std::function<bool(HWND, bool)> disable;   // DWMWA_TRANSITIONS_FORCEDISABLED
    std::function<bool(HWND)> alive;           // IsWindow
};
// Vraie API : l'attribut, plus une marque sur la fenêtre (propriété) tant qu'il est coupé.
TransitionApi realTransitionApi();
bool transitionsMarked(HWND window);
// Au démarrage du Dock : animations rendues aux fenêtres de premier niveau qu'un Dock précédent (planté, tué) a
// laissées marquées. `onlyProcess` : seulement celles de ce processus (0 : toutes). Renvoie leur nombre.
int releaseOrphanTransitions(DWORD onlyProcess = 0);

// Fenêtre retenue, vue par le Dock.
struct HeldState {
    bool iconic = false;
    bool appHidden = false;      // son app est masquée (« Masquer »)
    bool genieArmed = false;     // génie armé sur elle (appui sur « réduire »)
    bool genieRunning = false;   // génie en cours sur elle
};
// Retenue encore : génie armé ou en cours sur elle, ou réduite par « Masquer » (elle reviendra d'un coup, comme sur
// macOS). Réduite par le génie : rendue une fois le génie fini, pour qu'une restauration hors du Dock (⌘Tab, Alt+Tab)
// garde l'animation de Windows ; le Dock la recoupe avant son propre génie de restauration.
bool transitionBusy(const HeldState& s);
// Garée jusqu'à son retour (réduite et masquée) : la minuterie n'a plus à la surveiller.
bool transitionParked(const HeldState& s);

class TransitionGate {
public:
    explicit TransitionGate(TransitionApi api) : api_(std::move(api)) {}
    // Coupe les animations de la fenêtre (une fois) et la retient au moins jusqu'à `until` (secondes).
    void hold(HWND window, double until);
    // Rend les fenêtres dont l'échéance est passée et que `busy` ne retient plus (réduite, génie en cours…) ; oublie
    // les fenêtres fermées.
    void release(double now, const std::function<bool(HWND)>& busy);
    void releaseAll();   // arrêt du Dock
    bool held(HWND window) const { return held_.count(window) != 0; }
    bool any() const { return !held_.empty(); }
    std::vector<HWND> windows() const;

private:
    TransitionApi api_;
    std::map<HWND, double> held_;
};

} // namespace md
