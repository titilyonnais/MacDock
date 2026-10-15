// Fenêtre de premier niveau d'une autre app, lue pour lui donner l'apparence de macOS (coins, liseré, barre de titre) :
// style, cadre visible, zone client, processus, écran.
#pragma once
#include <windows.h>

#include <string>

namespace md {

struct WindowInfo {
    LONG style = 0, exStyle = 0;
    UINT classStyle = 0;
    std::wstring className;
    RECT frame{};    // cadre visible (DWMWA_EXTENDED_FRAME_BOUNDS), pixels écran
    RECT client{};   // zone client, pixels écran
    bool zoomed = false, iconic = false, ownProcess = false;
    bool elevated = false;    // processus d'intégrité plus élevée : nos attributs seraient refusés (UIPI)
    bool ownLights = false;   // fenêtre de MacDock qui dessine ses pastilles (own_lights.h) : elle règle son cadre elle-même
    RECT monitor{};           // écran de la fenêtre (rcMonitor) ; vide = inconnu
};

UINT effectiveDpi(HWND h);   // DPI réel de l'écran de la fenêtre
WindowInfo readInfo(HWND h);  // style, cadre, zone client, processus… d'une fenêtre de premier niveau

} // namespace md
