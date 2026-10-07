// Préférences utilisateur du Dock (settings.json).
#pragma once
#include <string>
#include <vector>

#include "../core/json.h"

namespace md {

enum class DockPosition { Bottom, Left, Right };
enum class PinKind { App, AppsButton, Stack };
// Pile : présentation du contenu (automatique = éventail jusqu'à 9 éléments, grille au-delà) et tri.
enum class StackView { Auto, Fan, Grid };
enum class StackSort { DateAdded, Name, Modified, Kind };

struct PinnedEntry {
    PinKind kind = PinKind::App;
    std::wstring appId;    // App : identifiant de regroupement (AUMID ou chemin exe en minuscules)
    std::wstring launch;   // App : cible lancée ; Stack : chemin du dossier
    std::wstring name;
    std::wstring exePath;  // App : exécutable cible, pour rattacher les fenêtres sans AUMID
    StackView stackView = StackView::Auto;        // Stack
    StackSort stackSort = StackSort::DateAdded;   // Stack
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
    std::wstring font;        // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    std::wstring screen;      // écran du Dock (nom GDI, ex. \\.\DISPLAY2) ; vide = principal
    std::vector<PinnedEntry> pinned;
    bool pinnedInitialized = false;  // false => importer les épingles par défaut
};

constexpr int kSettingsVersion = 2;

Settings settingsFromJson(const json::Value& v);
json::Value settingsToJson(const Settings& s);   // écrit "version": kSettingsVersion
// Fichier sans "version" (v1) : largeSize 128 (ancien défaut) → 80 ; met "version": 2.
json::Value migrateSettingsJson(const json::Value& v);

} // namespace md
