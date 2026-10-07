// Préférences utilisateur du Dock (settings.json).
#pragma once
#include <array>
#include <string>
#include <vector>

#include "../core/json.h"
#include "hot_corner_action.h"

namespace md {

enum class DockPosition { Bottom, Left, Right };
// Réduction des fenêtres : effet génie ou échelle (comme macOS), ou animation de Windows (rien n'est coupé).
enum class MinimizeEffect { Genie, Scale, Windows };
enum class PinKind { App, AppsButton, Stack };
// Pile : présentation du contenu (automatique = éventail jusqu'à 9 éléments, grille au-delà) et tri.
enum class StackView { Auto, Fan, Grid, List };   // List : jamais choisie automatiquement
enum class StackSort { DateAdded, Name, Modified, Kind };
// Pile : icône dans le Dock = ses derniers éléments empilés (macOS par défaut) ou l'icône du dossier.
enum class StackDisplay { Stack, Folder };

struct PinnedEntry {
    PinKind kind = PinKind::App;
    std::wstring appId;    // App : identifiant de regroupement (AUMID ou chemin exe en minuscules)
    std::wstring launch;   // App : cible lancée ; Stack : chemin du dossier
    std::wstring name;
    std::wstring exePath;  // App : exécutable cible, pour rattacher les fenêtres sans AUMID
    StackView stackView = StackView::Auto;        // Stack
    StackSort stackSort = StackSort::DateAdded;   // Stack
    StackDisplay stackDisplay = StackDisplay::Stack;   // Stack
};

struct Settings {
    DockPosition position = DockPosition::Bottom;
    bool autohide = false;
    bool magnification = true;
    bool showRecents = true;
    bool tahoeStrictIcons = true;
    double tileSize = 48;     // borné à [16, 128]
    double largeSize = 80;    // borné à [tileSize, 128]
    bool glass = true;        // verre Liquid Glass (capture de l'arrière-plan) ; false = verre dépoli simple
    MinimizeEffect minimizeEffect = MinimizeEffect::Genie;   // réduction des fenêtres dans le Dock
    std::wstring font;        // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    std::wstring screen;      // écran du Dock (nom GDI, ex. \\.\DISPLAY2) ; vide = principal
    std::wstring spotlightHotkey = L"alt+space";   // Spotlight : alt+space, ctrl+space ou off
    std::wstring missionControlHotkey = L"ctrl+alt+up";   // Mission Control : ctrl+alt+up, ctrl+up, f3 ou off
    std::wstring appSwitcherHotkey = L"alt+tab";          // sélecteur d'apps : alt+tab ou off
    // Coins actifs, indexés par Corner (haut gauche, haut droit, bas gauche, bas droit) ; aucun par défaut (un coin
    // actif surprend : l'horloge et le logo sont tout près).
    std::array<HotCornerAction, 4> hotCorners{HotCornerAction::Off, HotCornerAction::Off, HotCornerAction::Off,
                                              HotCornerAction::Off};
    std::vector<PinnedEntry> pinned;
    bool pinnedInitialized = false;  // false => importer les épingles par défaut
};

constexpr int kSettingsVersion = 2;

Settings settingsFromJson(const json::Value& v);
json::Value settingsToJson(const Settings& s);   // écrit "version": kSettingsVersion
// Fichier sans "version" (v1) : largeSize 128 (ancien défaut) → 80 ; met "version": 2.
json::Value migrateSettingsJson(const json::Value& v);

} // namespace md
