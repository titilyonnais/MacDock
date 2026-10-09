// Durées des animations de macOS 26 Tahoe, la cible de design (plan 44), réunies pour être recalées sur la VM de
// référence. Golden Gate (macOS 27) les raccourcissait d'environ 12 %. Génie et échelle : minimizeDuration (genie.cpp) ;
// masquage du Dock et « poof » : dock-metrics.json (réglables).
#pragma once

namespace md::tahoe {

constexpr double kMissionSeconds = 0.30;           // Mission Control : ouverture et fermeture
constexpr double kLaunchpadAppearSeconds = 0.20;   // écran Apps : apparition
constexpr double kStackFanSeconds = 0.24;          // pile en éventail : les icônes jaillissent le long de l'arc
constexpr double kStackGridFadeSeconds = 0.14;     // pile en grille : fondu
constexpr double kSpotlightAppearSeconds = 0.12;   // Spotlight : apparition
// Invite du champ de Spotlight (macOS 27 : « Rechercher ou demander »).
constexpr wchar_t kSpotlightPlaceholder[] = L"Recherche Spotlight";

} // namespace md::tahoe
