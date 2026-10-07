// Coins actifs (logique pure) : coins réels du bureau sous le pointeur, déclenchement à l'entrée, réarmement.
#pragma once
#include <windows.h>

#include <optional>
#include <vector>

#include "../config/hot_corner_action.h"

namespace md {

// Coin (zone de 2 px) d'un écran où le pointeur bute dans les deux sens : les trois points voisins vers l'extérieur
// ne sont sur aucun écran. Ailleurs (coin contre un autre écran), le pointeur glisserait vers l'écran voisin.
std::optional<Corner> cornerAt(POINT pt, const std::vector<RECT>& monitors);

// Déclenche une fois à l'entrée dans un coin ; il faut ensuite s'éloigner de plus de 24 px pour réarmer. Un coin
// atteint bloqué (bouton enfoncé, plein écran, vue modale) ne déclenche pas et demande aussi d'en ressortir.
class HotCornerTracker {
public:
    std::optional<Corner> update(std::optional<Corner> at, POINT pt, bool blocked);

private:
    bool armed_ = true;
    POINT last_{};   // point du dernier coin touché
};

} // namespace md
