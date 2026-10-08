// Lecture des sons système façon macOS (Dock et barre de menus).
#pragma once
#include "sound_synth.h"

namespace md {

// Joue le son sans attendre (PlaySound en mémoire) ; un nouveau son coupe le précédent. Fil de l'interface.
void playSystemSound(SystemSound s);

} // namespace md
