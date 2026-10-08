// Palette et cotes de l'app Réglages, façon Réglages Système de macOS 27 (docs/recherches/2026-10-08-cotes-macos.md).
// Couleurs en flottants non prémultipliés (r, g, b, a de 0 à 1) ; cotes en points (DIP).
#pragma once
#include <cstdint>

namespace md::ui {

struct Rgba {
    float r = 0, g = 0, b = 0, a = 1;
};
constexpr Rgba rgb(std::uint32_t hex, float a = 1) {
    return {float((hex >> 16) & 0xFF) / 255.f, float((hex >> 8) & 0xFF) / 255.f, float(hex & 0xFF) / 255.f, a};
}

struct Palette {
    Rgba window;            // fond du contenu (opaque)
    Rgba sidebarTint;       // voile posé sur le fond acrylique de la barre latérale
    Rgba sidebarSelection;  // section choisie
    Rgba group;             // fond des groupes arrondis
    Rgba separator;
    Rgba text, secondaryText, tertiaryText;
    Rgba accent;
    Rgba onAccent;          // texte et coche sur l'accent
    Rgba switchOff;         // piste d'un interrupteur éteint, et d'un curseur
    Rgba knob, knobEdge;    // bouton blanc et son liseré
    Rgba controlFill;       // fond d'un menu déroulant, piste d'un contrôle segmenté
    Rgba segmentSelected;
    Rgba focusRing;
    Rgba menuBackground, menuEdge;
    Rgba shadow;
};
Palette palette(bool dark);

namespace metrics {
constexpr float windowWidth = 715, minHeight = 470, defaultHeight = 620;
constexpr float sidebarWidth = 215, titleBar = 52;
constexpr float contentTop = titleBar + 8;          // premier groupe
constexpr float contentMargin = 20;                 // marges latérales des groupes
constexpr float groupRadius = 12, groupGap = 18, groupTitle = 26, footerGap = 6, footerHeight = 30;
constexpr float rowHeight = 36, rowHeightDetail = 48, rowHeightSlider = 56, rowPadding = 12;
constexpr float sidebarTop = titleBar + 40;         // sous le champ de recherche
constexpr float sidebarRow = 28, sidebarGroupGap = 10, sidebarInset = 10, tile = 20, tileRadius = 5;
constexpr float searchHeight = 28;
constexpr float switchWidth = 32, switchHeight = 18;
constexpr float sliderWidth = 220, knob = 20, sliderTrack = 4;
constexpr float popupHeight = 22, segmentHeight = 22, segmentPadding = 14;
constexpr float menuItem = 22, menuPadding = 6, menuRadius = 10;
constexpr float fontBody = 13, fontDetail = 11, fontTitle = 17, fontGroupTitle = 13;
constexpr float bottomPadding = 24;
}  // namespace metrics

}  // namespace md::ui
