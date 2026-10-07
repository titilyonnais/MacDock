// Réglages de la barre de menus (menubar.json) : lecture bornée, écriture (logique pure).
#pragma once
#include <string>

#include "../core/json.h"
#include "clock_format.h"

namespace md {

struct MenuBarMetrics {   // points
    double height = 24, fontSize = 13, leftMargin = 10, titlePadding = 10, logoSize = 14;
    double highlightHeight = 22, highlightRadius = 6, statusWidth = 30, rightMargin = 10;
};

struct MenuBarSettings {
    bool autohide = false;   // pas de zone réservée ; la barre apparaît quand le curseur touche le haut de l'écran
    std::wstring font;       // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    ClockOptions clock;
    bool showSound = true;
    MenuBarMetrics metrics;
};

constexpr int kMenuBarSettingsVersion = 1;

MenuBarSettings menuBarSettingsFromJson(const json::Value& v);   // valeurs bornées, défauts si absentes ou invalides
json::Value menuBarSettingsToJson(const MenuBarSettings& s);

} // namespace md
