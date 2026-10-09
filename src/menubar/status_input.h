// Menu de saisie du clavier (plan 58), comme celui de macOS : dispositions de la session, celle de l'app au premier
// plan, et son changement. Les fonctions de lecture appellent Windows ; layoutCode est pure (pays injecté).
#pragma once
#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace md {

struct InputLayout {
    std::uint32_t hkl = 0;   // 32 bits bas du HKL : langue (mot bas), disposition (mot haut)
    std::wstring name;       // « Français (France) »
    std::wstring code;       // « FR »
};

// Pays d'une langue (« FR », « US », « CH ») ; vide si inconnu.
std::wstring layoutCountry(unsigned short langid);
// Code à deux lettres de la disposition : le pays de sa disposition (mot haut quand c'est une langue), sinon de sa
// langue (dispositions variantes, mot haut en 0xF…).
std::wstring layoutCode(std::uint32_t hkl, const std::function<std::wstring(unsigned short)>& country = layoutCountry);
// Nom de la langue de la disposition, dans la langue de Windows (« Français (France) »).
std::wstring layoutName(std::uint32_t hkl);
// Dispositions de la session (GetKeyboardLayoutList), dans l'ordre de Windows.
std::vector<InputLayout> installedLayouts();
// Disposition d'une fenêtre : celle du fil qui a le clavier dans son app ; 0 sans fenêtre. La barre lit celle de l'app
// qu'elle suit (son propre menu peut avoir le premier plan).
std::uint32_t windowLayout(HWND window);
// Demande à cette fenêtre (à celle qui a son clavier) de passer à la disposition (WM_INPUTLANGCHANGEREQUEST, comme la
// barre de langue de Windows) ; false sans fenêtre ou si la disposition n'est pas installée.
bool switchWindowLayout(HWND window, std::uint32_t hkl);

std::wstring hklText(std::uint32_t hkl);           // « 040C040C »
std::uint32_t hklFromText(std::wstring_view text);   // 0 si illisible

} // namespace md
