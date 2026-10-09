// Fenêtres de MacDock qui dessinent leurs propres pastilles (app Réglages, Forcer à quitter) : une propriété de
// fenêtre les marque, et la barre de menus n'y pose ni ses pastilles ni son apparence (sinon elles seraient en double,
// par-dessus le titre). Plan 56.
#pragma once
#include <windows.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace md {

constexpr wchar_t kOwnLightsProp[] = L"MacDock.OwnLights";

// À poser avant le premier affichage de la fenêtre (sinon la barre peut y poser un calque jusqu'à sa sonde suivante)
// et à retirer au plus tard à WM_NCDESTROY.
inline void markOwnLights(HWND h) { SetPropW(h, kOwnLightsProp, reinterpret_cast<HANDLE>(1)); }
inline void unmarkOwnLights(HWND h) { RemovePropW(h, kOwnLightsProp); }
// Lu par la barre de menus, depuis un autre processus.
inline bool drawsOwnLights(HWND h) { return GetPropW(h, kOwnLightsProp) != nullptr; }

// Cadre de ces fenêtres, que la barre de menus ne règle plus pour elles (relecture du plan 56) : coins arrondis, sans
// liseré de Windows. false : le DWM a refusé un des deux attributs (le liseré ne se relit pas).
inline bool applyOwnFrame(HWND h) {
    const DWORD corner = 2;               // DWMWCP_ROUND
    const COLORREF border = 0xFFFFFFFE;   // DWMWA_COLOR_NONE
    const HRESULT a = DwmSetWindowAttribute(h, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &corner, sizeof corner);
    const HRESULT b = DwmSetWindowAttribute(h, 34 /* DWMWA_BORDER_COLOR */, &border, sizeof border);
    return SUCCEEDED(a) && SUCCEEDED(b);
}

} // namespace md
