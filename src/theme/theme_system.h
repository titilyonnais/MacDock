// Thème macOS sur le vrai Windows : registre des curseurs, IDesktopWallpaper, fichiers dans %APPDATA%\MacDock.
// COM doit être initialisé sur le thread appelant. Les tests n'appellent rien ici (API injectée : theme_apply.h).
#pragma once
#include <string>

#include "theme_apply.h"

namespace md {

ThemeApi realThemeApi();
std::wstring themeBackupPath();   // %APPDATA%\MacDock\theme-backup.json
bool themeBackupExists();         // le thème macOS est appliqué : on peut rétablir
ThemeResult applyMacTheme();        // fichiers dans %APPDATA%\MacDock\theme, sauvegarde écrite une seule fois
ThemeResult restoreWindowsTheme();  // rend la sauvegarde, puis l'efface si tout est rendu
// Planche des curseurs (32 et 64 px, fonds clair et sombre) et les deux fonds d'écran, sans rien appliquer.
bool writeThemeSnapshot(const std::wstring& dir);

} // namespace md
