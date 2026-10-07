// Application et rétablissement du thème macOS (curseurs et fond d'écran), à travers une API injectable.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../core/json.h"

namespace md {

// Ce que le thème remplace ; une valeur vide = absente à l'origine (curseur par défaut, fond sans fichier).
struct ThemeBackup {
    std::map<std::wstring, std::wstring> cursors;
    std::map<std::wstring, std::wstring> wallpapers;
};

struct ThemeApi {
    std::function<std::optional<std::wstring>(const std::wstring& name)> readCursor;   // HKCU\Control Panel\Cursors
    std::function<bool(const std::wstring& name, const std::wstring& value)> writeCursor;
    std::function<bool()> reloadCursors;
    std::function<std::vector<std::wstring>()> monitors;   // identifiants des écrans
    std::function<std::wstring(const std::wstring& id)> getWallpaper;
    std::function<bool(const std::wstring& id, const std::wstring& path)> setWallpaper;
    std::function<bool(const std::wstring& path, const std::vector<std::uint8_t>& bytes)> writeFile;
    std::function<bool()> darkMode;
    std::function<SIZE(const std::wstring& id)> monitorSize;
    // Facultatif : écrit la sauvegarde avant le premier changement ; un échec arrête tout.
    std::function<bool(const ThemeBackup& backup)> saveBackup;
    // Facultatif : le fichier existe encore (fond d'origine supprimé depuis → gardé, signalé).
    std::function<bool(const std::wstring& path)> fileExists;
};

struct ThemeResult {
    bool ok = false;
    std::wstring message;   // erreurs, ou remarques (fond d'origine inconnu) ; vide si rien à dire
    // Rétablissement : ce qui reste à rendre plus tard (écran débranché, refus de Windows) ; nullopt si rien.
    std::optional<ThemeBackup> remaining;
};

json::Value themeBackupToJson(const ThemeBackup& b);
std::optional<ThemeBackup> themeBackupFromJson(const json::Value& v);

// Ce que l'application change : tout, ou le fond d'écran seul (curseurs installés à part par l'utilisateur).
struct ThemeParts {
    bool cursors = true;
    bool wallpaper = true;
};

// Écrit les fichiers dans dir, sauvegarde l'état actuel si backup est vide, puis change curseurs et fonds.
ThemeResult applyTheme(ThemeApi& api, const std::wstring& dir, std::optional<ThemeBackup>& backup, const ThemeParts& parts = {});
// Rend les valeurs sauvegardées ; les écrans absents sont ignorés.
ThemeResult restoreTheme(ThemeApi& api, const ThemeBackup& backup, const std::wstring& dir);

} // namespace md
