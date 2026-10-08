// Animations de DWM d'une fenêtre (réduction, agrandissement, ouverture) coupées pour la seule fenêtre que le Dock
// anime lui-même (génie, échelle), puis rendues dès que plus rien ne la retient : l'agrandissement et la restauration
// des autres fenêtres gardent l'animation de Windows. DWMWA_TRANSITIONS_FORCEDISABLED se pose depuis un autre
// processus, mais seulement AVANT le changement d'état : posé 5 ms après, DWM joue quand même son animation (mesuré).
#pragma once
#include <windows.h>

#include <functional>
#include <map>

namespace md {

struct TransitionApi {
    std::function<bool(HWND, bool)> disable;   // DWMWA_TRANSITIONS_FORCEDISABLED
    std::function<bool(HWND)> alive;           // IsWindow
};
TransitionApi realTransitionApi();

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

private:
    TransitionApi api_;
    std::map<HWND, double> held_;
};

} // namespace md
