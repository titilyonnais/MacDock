// Fichiers de curseur Windows : .cur (plusieurs tailles) et .ani (animé), écrits en mémoire.
#pragma once
#include <cstdint>
#include <vector>

#include "cursor_art.h"

namespace md {

// Une image par taille ; point actif dans chaque entrée ; DIB 32 bits de bas en haut + masque ET à zéro.
std::vector<std::uint8_t> encodeCur(const std::vector<CursorFrame>& sizes);
// Animation : un .cur par image, cadence en soixantièmes de seconde.
std::vector<std::uint8_t> encodeAni(const std::vector<std::vector<std::uint8_t>>& curFiles, int jiffies);

} // namespace md
