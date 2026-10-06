// Préférences utilisateur du Dock (settings.json).
#pragma once
#include <string>
#include <vector>

#include "../core/json.h"

namespace md {

enum class DockPosition { Bottom, Left, Right };
enum class PinKind { App, AppsButton, Stack };

struct PinnedEntry {
    PinKind kind = PinKind::App;
    std::wstring appId;    // App : identifiant de regroupement (AUMID ou chemin exe en minuscules)
    std::wstring launch;   // App : cible lancée ; Stack : chemin du dossier
    std::wstring name;
};

struct Settings {
    DockPosition position = DockPosition::Bottom;
    bool autohide = false;
    bool magnification = true;
    bool showRecents = true;
    bool tahoeStrictIcons = true;
    double tileSize = 48;     // borné à [16, 128]
    double largeSize = 128;   // borné à [tileSize, 128]
    std::wstring font;        // vide = automatique (SF Pro > Inter > Segoe UI Variable)
    std::vector<PinnedEntry> pinned;
    bool pinnedInitialized = false;  // false => importer les épingles par défaut
};

Settings settingsFromJson(const json::Value& v);
json::Value settingsToJson(const Settings& s);

} // namespace md
