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

// Bas de la barre en pixels : yOffsetPx vaut heightPx quand la barre est masquée (masquage automatique, plein écran).
int hudBarBottom(int top, int heightPx, int yOffsetPx);

// Avis de luminosité : pastille seulement si le niveau a changé. Le premier avis (valeur courante, envoyée à
// l'enregistrement) ne fait que retenir le niveau, comme ceux qui arrivent pendant un menu, moins d'une seconde après
// un réglage fait par la barre, ou moins de deux secondes après un événement du système (sortie de veille, écran
// rallumé, secteur ou batterie).
class BrightnessGate {
public:
    bool accept(double now, bool menuOpen, int percent);
    void noteOwnChange(double now) { own_ = now; ownSet_ = true; }
    void noteSystemChange(double now) { system_ = now; systemSet_ = true; }

private:
    bool armed_ = false, ownSet_ = false, systemSet_ = false;
    int last_ = -1;
    double own_ = 0, system_ = 0;
};

} // namespace md
