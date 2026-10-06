// Rebonds d'icônes du Dock (fonctions pures). Décalages >= 0, dans l'unité de height.
#pragma once

namespace md {

// Rebond de lancement : parabole 4p(1-p) répétée à chaque période.
double launchBounceOffset(double elapsed, double period, double height);

// Rebond d'attention : `count` rebonds, puis une pause, en boucle.
double attentionBounceOffset(double elapsed, double period, double height, int count, double pause);

} // namespace md
