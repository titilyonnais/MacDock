#include "window_look.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace md {

bool shouldRoundCorners(DWORD original) { return original != 1 && original != 3; }

namespace {
constexpr const wchar_t* kShellClasses[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW"};
}

bool styledWindow(const WindowInfo& w) {
    if (w.iconic || w.ownProcess || w.elevated || w.ownLights) return false;
    // Menu système, ou au moins un bouton réduire ou agrandir (Electron sans cadre n'a pas de menu système).
    if ((w.style & WS_CAPTION) != WS_CAPTION || (w.style & WS_CHILD)) return false;
    if (!(w.style & (WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX))) return false;
    if (w.exStyle & WS_EX_TOOLWINDOW) return false;
    for (const wchar_t* c : kShellClasses)
        if (w.className == c) return false;
    return true;
}

bool squareInFullscreen(const WindowInfo& w) {
    const RECT& m = w.monitor;
    return !w.zoomed && (w.style & WS_CAPTION) == WS_CAPTION && m.right > m.left && w.frame.left <= m.left &&
           w.frame.top <= m.top && w.frame.right >= m.right && w.frame.bottom >= m.bottom;
}

bool appsDarkMode() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

bool useSmallCorners(const WindowInfo& w, UINT dpi, double modSmallPt) {
    if (modSmallPt < 8) return false;
    if (!(w.style & WS_THICKFRAME)) return true;   // taille fixe : boîte de dialogue
    const double k = (dpi ? dpi : 96) / 96.0;
    const double side = std::min(w.frame.right - w.frame.left, w.frame.bottom - w.frame.top) / k;
    return side < 300;
}

double cornerModSmallRadius() {
    const wchar_t* key = L"SOFTWARE\\Windhawk\\Engine\\Mods\\custom-corner-radius";
    DWORD disabled = 1, size = sizeof disabled;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, key, L"Disabled", RRF_RT_REG_DWORD, nullptr, &disabled, &size) != ERROR_SUCCESS ||
        disabled)
        return 0;
    DWORD v = 0;
    size = sizeof v;
    const std::wstring settings = std::wstring(key) + L"\\Settings";
    if (RegGetValueW(HKEY_LOCAL_MACHINE, settings.c_str(), L"smallRadius", RRF_RT_REG_DWORD, nullptr, &v, &size) != ERROR_SUCCESS ||
        int(v) < 0)
        return 0;
    return double(v);
}

std::optional<WindowLook> macWindowLook(const WindowInfo& w, bool dark, UINT dpi, int backdrop) {
    WindowInfo any = w;
    any.iconic = false;   // une fenêtre réduite garde son apparence pour son retour
    if (!styledWindow(any)) return std::nullopt;
    // Plein écran sans être agrandie (vidéo, jeu sans bordure) : rendue à Windows, coins carrés.
    const RECT& m = w.monitor;
    if (!w.zoomed && m.right > m.left && w.frame.left <= m.left && w.frame.top <= m.top && w.frame.right >= m.right &&
        w.frame.bottom >= m.bottom)
        return std::nullopt;
    WindowLook l;
    // Barre de titre dessinée par Windows (au moins 20 pt au-dessus de la zone client) : gris de macOS 27.
    if (backdrop <= 1 && w.client.top - w.frame.top >= std::lround(20.0 * (dpi ? dpi : 96) / 96)) {
        l.caption = true;
        l.captionColor = dark ? RGB(44, 43, 46) : RGB(236, 236, 238);
        l.textColor = dark ? RGB(228, 228, 230) : RGB(38, 38, 40);
    }
    return l;
}

} // namespace md
