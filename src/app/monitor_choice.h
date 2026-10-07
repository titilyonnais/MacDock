// Choix de l'écran du Dock : poussée du curseur contre le bord du Dock, écran enregistré (logique pure).
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "../config/settings.h"

namespace md {

struct MonitorInfo {
    std::wstring name;   // \\.\DISPLAYn
    RECT rect{};
    bool primary = false;
};

// Écran visé par une poussée au bord (nullopt si le curseur n'est contre le bord d'aucun écran).
// Seul un vrai bord compte : celui au-delà duquel aucun autre écran ne prolonge le bureau.
std::optional<std::size_t> pushedMonitor(const std::vector<MonitorInfo>& monitors, POINT cursor, DockPosition edge,
                                         int edgePx);
// Écran enregistré s'il existe encore (casse ignorée), sinon le principal (ou le premier).
std::size_t initialMonitor(const std::vector<MonitorInfo>& monitors, const std::wstring& saved);

} // namespace md
