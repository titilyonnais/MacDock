// Épingles du premier lancement.
#pragma once
#include <vector>

#include "../config/settings.h"

namespace md {

// Explorateur, Apps, épingles de la barre des tâches Windows (dans leur ordre), Téléchargements.
std::vector<PinnedEntry> defaultPins();

} // namespace md
