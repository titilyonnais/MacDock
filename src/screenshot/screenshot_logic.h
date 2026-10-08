// Captures d'écran façon macOS (logique pure) : noms de fichier, raccourcis ⊞⇧3 et ⊞⇧4, sélection, vignette
// flottante, coins arrondis et ombre des captures de fenêtre.
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "../core/bgra_image.h"

namespace md {

// « Capture d’écran 2026-10-08 à 09.05.07 », comme sur un Mac en français (apostrophe typographique).
std::wstring screenshotBaseName(const SYSTEMTIME& t);
// « base.png » pour n ≤ 1, sinon « base (n).png ».
std::wstring screenshotFileName(const std::wstring& base, int n);
// Premier nom libre dans dir à partir du numéro first (écrans suivants : 2, 3…) ; exists dit si un chemin est pris.
std::wstring uniqueScreenshotPath(const std::wstring& dir, const std::wstring& base, int first,
                                  const std::function<bool(const std::wstring&)>& exists);

// Crochet clavier : ⊞⇧3 (écran entier) et ⊞⇧4 (zone). Explorer garde ces raccourcis (RegisterHotKey : 1409).
struct ShotMods {
    bool win = false, shift = false, ctrl = false, alt = false;
};
struct ShotKeyEvent {
    unsigned vk = 0;
    bool down = false, repeat = false, injected = false;
    ShotMods mods;
    bool taken = false;   // l'appui de cette touche a été pris : son relâchement et ses répétitions aussi
};
enum class ShotKey { Pass, Screen, Region, Swallow };
ShotKey screenshotKey(const ShotKeyEvent& e);

// Pendant le viseur : Échap annule, Espace bascule entre zone et fenêtre ; les autres touches passent.
enum class ShotSessionKey { Pass, Cancel, ToggleWindow, Swallow };
ShotSessionKey screenshotSessionKey(unsigned vk, bool down, bool repeat);

// Rectangle tiré de a à b (pixels écran), normalisé et borné à l'écran où la sélection a commencé.
RECT selectionRect(POINT a, POINT b, const RECT& bounds);
bool selectionUsable(const RECT& r);   // au moins 4 × 4 pixels (un simple clic ne capture rien)

// Vignette flottante : l'image tient dans 200 × 150 points sans jamais être agrandie, à 20 points du coin bas
// droit de la zone de travail. Renvoie le rectangle de l'image (pixels écran).
RECT thumbnailRect(const RECT& work, SIZE image, double scale);
constexpr double kThumbIn = 0.30, kThumbStay = 5.0, kThumbOut = 0.25;   // secondes
enum class ThumbPhase { In, Out };
// Décalage vers la droite (pixels) à t secondes de la phase : entrée qui ralentit, sortie qui accélère.
double thumbnailOffset(ThumbPhase phase, double t, double distance);

// ---- Images (BgraImage : alpha non prémultiplié) ----
void roundCorners(BgraImage& img, double radius);   // hors de l'arrondi : transparent (bord anti-crénelé)
struct ShadowSpec {
    int left = 0, top = 0, right = 0, bottom = 0;   // marges ajoutées autour de l'image
    double blur = 0;                                 // écart du flou (pixels)
    double opacity = 0;
    int offsetY = 0;                                 // ombre décalée vers le bas
};
ShadowSpec windowShadowSpec(double scale);   // grande ombre douce de macOS, plus marquée sous la fenêtre
BgraImage withShadow(const BgraImage& img, const ShadowSpec& spec);   // ombre noire floue sous l'image
std::vector<std::uint8_t> premultiply(const BgraImage& img);          // BGRA prémultiplié (PNG, fenêtres en couches)
// Vignette : image déjà réduite → coins arrondis, liseré, petite ombre ; BGRA prémultiplié de w × h pixels, l'image
// commençant à (margin, margin).
std::vector<std::uint8_t> thumbnailPixels(const BgraImage& small, double scale, int& w, int& h, int& margin);
// Curseur appareil photo du mode fenêtre (corps noir, contour blanc), dessiné par le code : BGRA non prémultiplié.
std::vector<std::uint8_t> cameraCursorPixels(int size);

} // namespace md
