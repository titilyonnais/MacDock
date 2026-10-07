// Mission Control (logique pure) : rangement des fenêtres sans chevauchement, test de clic, animation, raccourci.
#pragma once
#include <optional>
#include <string>
#include <vector>

#include "../spotlight/spot_results.h"

namespace md {

struct MissionRect {
    double x = 0, y = 0, w = 0, h = 0;
};

constexpr double kMissionGap = 24;   // écart entre fenêtres, en points

// Zone de rangement : work (pixels) moins 48 pt à gauche, à droite et en bas, 64 pt en haut.
MissionRect missionArea(const MissionRect& work, double scale);

// Une place par fenêtre (même ordre) : lignes à échelle commune, ordre de lecture d'après la place réelle,
// proportions gardées, jamais agrandies, tout dans area, écart gap entre fenêtres.
std::vector<MissionRect> missionLayout(const std::vector<MissionRect>& windows, const MissionRect& area, double gap);

int missionHit(const std::vector<MissionRect>& rects, double x, double y);   // -1 : aucune
MissionRect lerpRect(const MissionRect& a, const MissionRect& b, double t);
double easeOut(double t);   // cubique, t borné à [0, 1]

// « ctrl+alt+up », « ctrl+up », « f3 » (casse ignorée) ; nullopt pour « off » ou une valeur inconnue.
std::optional<HotkeySpec> parseMissionHotkey(const std::wstring& text);

} // namespace md
