// Feux tricolores (logique pure) : quelles fenêtres en ont, où les poser, ce qu'un clic y fait, et leur image.
#pragma once
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "menubar_settings.h"

namespace md {

struct LightsWindowInfo {
    LONG style = 0, exStyle = 0;
    UINT classStyle = 0;
    std::wstring className;
    RECT frame{};    // cadre visible (DWMWA_EXTENDED_FRAME_BOUNDS), pixels écran
    RECT client{};   // zone client, pixels écran
    bool zoomed = false, iconic = false, ownProcess = false;
};

// Standard : seulement si Windows dessine la barre de titre (la zone client commence au moins 20 pt plus bas).
bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT dpi);

struct LightsLayout {
    RECT window{};       // le calque (pixels écran)
    RECT circles[3]{};   // fermer, réduire, zoom (pixels écran)
    double radius = 0;   // pixels
    RECT patch{};        // fond de la couleur de la barre de titre
};
LightsLayout lightsLayout(const RECT& frame, const RECT& client, UINT dpi);

int hitLight(const LightsLayout& l, POINT screen);   // 0 fermer, 1 réduire, 2 zoom, -1 ailleurs
UINT lightCommand(int light, bool zoomed);            // SC_CLOSE, SC_MINIMIZE, SC_MAXIMIZE ou SC_RESTORE
// Couleur la plus fréquente (à 8 niveaux près par canal), 0xRRGGBB ; 0 sans échantillon.
std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples);

struct LightsState {
    bool hover = false;                          // symboles ×, −, + (survol du groupe)
    bool enabled[3] = {true, true, true};        // indisponible : gris, sans action
    bool dark = false;                           // thème de la barre de titre (gris des pastilles indisponibles)
    std::uint32_t patchColor = 0xF3F3F3;         // 0xRRGGBB, couleur de la barre de titre
};
// Image BGRA prémultipliée du calque (taille de l.window) ; scale = dpi / 96.
std::vector<std::uint8_t> renderLights(const LightsLayout& l, const LightsState& s, double scale);
// Planche hors écran (BGRA opaque) : thème clair puis sombre ; normal, survol, indisponible ; à 200 %.
std::vector<std::uint8_t> lightsSheet(UINT& w, UINT& h);

} // namespace md
