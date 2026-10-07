// HUD du volume et de la luminosité (logique pure) : pas du volume, fondu, place de la pastille, garde de la luminosité.
#pragma once
#include <windows.h>

#include <string>

namespace md {

enum class HudKind { Volume, Brightness };

struct HudContent {
    HudKind kind = HudKind::Volume;
    float level = 0;        // 0..1
    bool muted = false;     // volume seulement
    std::wstring detail;    // nom de la sortie audio (vide : rien)
};

// Pas des touches de volume : grille de 1/16 (1/64 si fine), dir +1 ou -1, résultat borné à [0, 1].
float volumeStep(float v, int dir, bool fine);

// Visible kHold secondes après le dernier changement, puis fondu linéaire de kFade secondes.
class HudFade {
public:
    static constexpr double kHold = 1.5, kFade = 0.25;
    void show(double now) { last_ = now; shown_ = true; }
    float opacity(double now) const;
    bool visible(double now) const { return opacity(now) > 0; }
    void reset() { shown_ = false; }

private:
    double last_ = 0;
    bool shown_ = false;
};

struct HudPlace {   // pixels de l'écran
    int x = 0, y = 0, w = 0, h = 0;
};
// 280 × 64 pt, à 12 pt du bord droit de l'écran et 8 pt sous la barre (barBottom : bas de la barre, en pixels).
HudPlace hudPlace(const RECT& monitor, int barBottom, float scale);

// Avis de luminosité : le premier (valeur courante, envoyé à l'enregistrement) est ignoré, comme ceux qui arrivent
// pendant un menu ou moins d'une seconde après un réglage fait par la barre.
class BrightnessGate {
public:
    bool accept(double now, bool menuOpen);
    void noteOwnChange(double now) { own_ = now; ownSet_ = true; }

private:
    bool armed_ = false, ownSet_ = false;
    double own_ = 0;
};

} // namespace md
