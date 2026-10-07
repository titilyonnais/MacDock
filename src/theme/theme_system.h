// Thème macOS sur le vrai Windows : registre des curseurs, IDesktopWallpaper, fichiers dans %APPDATA%\MacDock.
// COM doit être initialisé sur le thread appelant. Les tests n'appellent rien ici (API injectée : theme_apply.h).
#pragma once
#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "theme_apply.h"

namespace md {

ThemeApi realThemeApi();
std::wstring themeBackupPath();   // %APPDATA%\MacDock\theme-backup.json
std::wstring themeDir();          // %APPDATA%\MacDock\theme : nos curseurs et fonds
bool themeBackupExists();         // le thème macOS est appliqué : on peut rétablir
ThemeResult applyMacTheme();        // fichiers dans %APPDATA%\MacDock\theme, sauvegarde écrite une seule fois
ThemeResult restoreWindowsTheme();  // rend la sauvegarde ; l'efface si tout est rendu, sinon garde le reste
// Planche des curseurs (32 et 64 px, fonds clair et sombre) et les deux fonds d'écran, sans rien appliquer.
bool writeThemeSnapshot(const std::wstring& dir);

// Application ou rétablissement hors du fil de l'interface (en Debug, plusieurs secondes) : le résultat arrive
// par PostMessage(notify, msg, 0, lParam) ; ThemeJob::take(lParam) le reprend. Une seule tâche à la fois.
class ThemeJob {
public:
    ~ThemeJob();
    bool start(std::function<ThemeResult()> work, HWND notify, UINT msg);
    bool busy() const { return busy_; }
    static ThemeResult take(LPARAM lp);

private:
    std::thread thread_;
    std::atomic<bool> busy_{false};
};

} // namespace md
