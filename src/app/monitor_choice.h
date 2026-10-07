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

// Poussée contre le bord du Dock sur un autre écran : la souris doit continuer de bouger contre le bord
// (événements reçus sans interruption) pendant kPushSeconds ; une souris simplement posée au bord n'en
// produit aucun et ne déplace pas le Dock.
class ScreenPush {
public:
    static constexpr double kPushSeconds = 0.35;
    static constexpr double kMaxSilence = 0.15;   // un silence plus long recommence la poussée
    // À chaque événement souris : écran visé (vide = aucun). Renvoie l'écran à adopter, une fois, à la confirmation.
    std::wstring update(const std::wstring& target, double now);

private:
    std::wstring target_;
    double since_ = 0, last_ = 0;
};

} // namespace md
