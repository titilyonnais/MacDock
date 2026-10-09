// Fenêtres de MacDock qui dessinent leurs propres pastilles (app Réglages, Forcer à quitter) : une propriété de
// fenêtre les marque, et la barre de menus n'y pose ni ses pastilles ni son apparence (sinon elles seraient en double,
// par-dessus le titre). Plan 56.
#pragma once
#include <windows.h>

namespace md {

constexpr wchar_t kOwnLightsProp[] = L"MacDock.OwnLights";

// À poser après la création de la fenêtre et à retirer avant sa destruction (WM_NCDESTROY).
inline void markOwnLights(HWND h) { SetPropW(h, kOwnLightsProp, reinterpret_cast<HANDLE>(1)); }
inline void unmarkOwnLights(HWND h) { RemovePropW(h, kOwnLightsProp); }
// Lu par la barre de menus, depuis un autre processus.
inline bool drawsOwnLights(HWND h) { return GetPropW(h, kOwnLightsProp) != nullptr; }

} // namespace md
