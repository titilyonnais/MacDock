// Coins actifs : coins de l'écran et actions possibles (noms du réglage hotCorners de settings.json).
#pragma once
#include <optional>
#include <string>

namespace md {

enum class Corner { TopLeft, TopRight, BottomLeft, BottomRight };
enum class HotCornerAction { Off, MissionControl, Desktop, Apps, NotificationCenter, LockScreen, DisplaySleep, ScreenSaver };

// « missionControl », « desktop »… (casse ignorée) ; nullopt pour une valeur inconnue.
std::optional<HotCornerAction> parseHotCornerAction(const std::wstring& text);
std::wstring hotCornerName(HotCornerAction action);

} // namespace md
