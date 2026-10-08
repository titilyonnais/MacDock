// Réglages de la barre de menus (menubar.json) : lecture bornée, écriture (logique pure).
#pragma once
#include <string>

#include "../core/json.h"
#include "clock_format.h"

namespace md {

// Feux tricolores : fenêtres à barre de titre Windows (défaut), toutes, ou aucune.
enum class LightsMode { Standard, All, Off };

struct MenuBarMetrics {   // points
    double height = 24, fontSize = 13, leftMargin = 10, titlePadding = 10, logoSize = 14;
    double highlightHeight = 22, highlightRadius = 6, statusWidth = 30, rightMargin = 10;
    double statusIconSize = 16;   // pictogrammes d'état
};

struct MenuBarSettings {
    bool autohide = false;   // pas de zone réservée ; la barre apparaît quand le curseur touche le haut de l'écran
    std::wstring font;       // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    ClockOptions clock;
    bool showSound = true;
    bool showNetwork = true;    // Wi-Fi (masqué sans carte Wi-Fi)
    bool showBattery = true;    // masqué sans batterie
    bool showSearch = true;
    bool volumeFeedback = true;   // « pop » quand le volume change au clavier, comme sur macOS
    bool hud = true;            // pastille du volume et de la luminosité ; reprend les touches de volume
    bool showAppIcons = true;   // icônes des autres apps (relayées par le mod Windhawk)
    LightsMode trafficLights = LightsMode::Standard;   // pastilles fermer, réduire, zoom, à la place des boutons de Windows
    bool macWindows = true;     // coins arrondis, sans liseré coloré, barre de titre grise pour les autres apps
    MenuBarMetrics metrics;
};

constexpr int kMenuBarSettingsVersion = 1;

MenuBarSettings menuBarSettingsFromJson(const json::Value& v);   // valeurs bornées, défauts si absentes ou invalides
json::Value menuBarSettingsToJson(const MenuBarSettings& s);

} // namespace md
