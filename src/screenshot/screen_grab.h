// Captures d'écran : copie du bureau composé, vue d'une fenêtre seule, Bureau de l'utilisateur, PNG, presse-papiers.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

#include "../core/bgra_image.h"

namespace md {

// Pixels du bureau composé dans r (pixels physiques de l'écran virtuel), alpha 255. Les fenêtres exclues des
// captures (WDA_EXCLUDEFROMCAPTURE) n'y sont pas : ce qu'elles couvrent apparaît à leur place.
BgraImage grabScreen(const RECT& r);
// La fenêtre seule, même recouverte (PrintWindow, PW_RENDERFULLCONTENT), rognée à ses bords visibles ; frame reçoit
// ces bords (écran). Vide si Windows refuse.
BgraImage grabWindow(HWND window, RECT& frame);
// Bords visibles d'une fenêtre (DWM, sans la marge invisible de redimensionnement), sinon GetWindowRect.
RECT windowFrameBounds(HWND window);
// Rayon des coins arrondis de Windows 11 (8 pixels à 100 %, selon l'écran) ; 0 si agrandie ou coins droits.
int windowCornerRadius(HWND window);
double monitorScale(HMONITOR monitor);   // échelle de l'écran (1 à 100 %, 2 à 200 %)
// Aucune fenêtre visible au-dessus de window ne touche frame. ignore : fenêtres à ne pas compter (les nôtres).
bool windowUnobscured(HWND window, const RECT& frame, const std::function<bool(HWND)>& ignore);
// Fenêtre de premier niveau visible sous le point (z-order), hors fenêtres ignorées, transparentes aux clics ou
// cachées par DWM ; nullptr si aucune (bureau).
HWND topWindowAt(POINT pt, const std::function<bool(HWND)>& ignore);
std::wstring desktopFolder();   // Bureau de l'utilisateur (redirigé vers OneDrive le cas échéant)
// PNG avec alpha non prémultiplié. COM initialisé sur le fil appelant.
bool saveScreenshotPng(const BgraImage& img, const std::wstring& path);
bool copyImageToClipboard(HWND owner, const BgraImage& img);   // CF_DIB 32 bits, opaque

} // namespace md
