#include "window_look.h"

#include <cmath>

namespace md {

bool shouldRoundCorners(DWORD original) { return original != 1 && original != 3; }

bool appsDarkMode() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

std::optional<WindowLook> macWindowLook(const LightsWindowInfo& w, bool dark, UINT dpi, int backdrop) {
    LightsWindowInfo any = w;
    any.iconic = false;   // une fenêtre réduite garde son apparence pour son retour
    if (!wantsLights(any, LightsMode::All, dpi)) return std::nullopt;
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
