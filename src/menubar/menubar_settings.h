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
    double highlightHeight = 22, highlightRadius = 6, statusWidth = 36, rightMargin = 9;
    double statusIconSize = 16;   // pictogrammes d'état
    // Ligne de base du texte, depuis le haut d'une barre de 24 (macOS 26 : 16) ; une barre plus haute ou plus basse garde
    // le texte au même écart de son milieu.
    double textBaseline = 16;
    double clockGap = 3;   // écart de plus avant l'horloge
};

struct MenuBarSettings {
    bool autohide = false;   // pas de zone réservée ; la barre apparaît quand le curseur touche le haut de l'écran
    std::wstring font;       // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    ClockOptions clock;
    bool showSound = true;
    bool showNetwork = true;    // Wi-Fi (masqué sans carte Wi-Fi)
    bool showBattery = true;    // masqué sans batterie
    bool showSearch = true;
    bool showInput = true;      // menu de saisie du clavier (masqué avec une seule disposition)
    bool volumeFeedback = true;   // « pop » quand le volume change au clavier, comme sur macOS
    bool hud = true;            // pastille du volume et de la luminosité ; reprend les touches de volume
    bool showAppIcons = true;   // icônes des autres apps (relayées par le mod Windhawk)
    LightsMode trafficLights = LightsMode::Standard;   // pastilles fermer, réduire, zoom, à la place des boutons de Windows
    bool macWindows = true;     // coins arrondis, sans liseré coloré, barre de titre grise pour les autres apps
    MenuBarMetrics metrics;
};

// v2 : mesures de la barre de macOS 26 (plan 64).
constexpr int kMenuBarSettingsVersion = 2;

MenuBarSettings menuBarSettingsFromJson(const json::Value& v);   // valeurs bornées, défauts si absentes ou invalides
json::Value menuBarSettingsToJson(const MenuBarSettings& s);
// Fichier d'une version plus ancienne : une mesure encore à l'ancien défaut prend le nouveau, une valeur choisie reste ;
// le reste est gardé tel quel. Déjà à jour, ou pas un objet : rendu tel quel.
json::Value migrateMenuBarSettingsJson(const json::Value& v);

} // namespace md
