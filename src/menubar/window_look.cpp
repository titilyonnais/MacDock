#include "window_look.h"

#include <cmath>

namespace md {

std::optional<WindowLook> macWindowLook(const LightsWindowInfo& w, bool dark, UINT dpi) {
    LightsWindowInfo any = w;
    any.iconic = false;   // une fenêtre réduite garde son apparence pour son retour
    if (!wantsLights(any, LightsMode::All, dpi)) return std::nullopt;
    WindowLook l;
    // Barre de titre dessinée par Windows (au moins 20 pt au-dessus de la zone client) : gris de macOS 27.
    if (w.client.top - w.frame.top >= std::lround(20.0 * (dpi ? dpi : 96) / 96)) {
        l.caption = true;
        l.captionColor = dark ? RGB(44, 43, 46) : RGB(236, 236, 238);
        l.textColor = dark ? RGB(228, 228, 230) : RGB(38, 38, 40);
    }
    return l;
}

} // namespace md
