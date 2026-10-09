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
// Clé du clavier dans le registre (« Keyboard Layouts ») : « 0000 » et le mot haut quand c'est une langue ; pour une
// variante (mot haut 0xF…), la clé dont « Layout Id » vaut ses 12 bits bas (variantKlid) ; pour une IME (0xE…), le HKL
// entier ; vide si inconnue.
std::wstring layoutKlid(std::uint32_t hkl, const std::function<std::wstring(unsigned short)>& variantKlid);
// Nom complet, comme la liste de Windows : la langue de saisie (mot bas) et le nom du clavier, dans la langue de
// Windows (« Français (France) – Français (Standard, BÉPO) ») ; le HKL en texte si le clavier n'a pas de nom.
std::wstring layoutFullName(std::uint32_t hkl);
// Deux lignes du même nom (deux claviers d'une langue, une langue sur deux claviers) : chacune prend son nom complet.
void distinguishLayouts(std::vector<InputLayout>& list, const std::function<std::wstring(std::uint32_t)>& fullName = layoutFullName);
// Dispositions de la session (GetKeyboardLayoutList), dans l'ordre de Windows.
std::vector<InputLayout> installedLayouts();
// Fenêtre dont le fil porte la disposition : une console (conhost) est attribuée par Windows à son processus client
// (cmd, pwsh), qui ne traite jamais le clavier ; c'est sa fenêtre IME (imeWindow) qui appartient à conhost.
HWND layoutWindow(HWND window, std::wstring_view className, const std::function<HWND(HWND)>& imeWindow);
// Disposition d'une fenêtre : celle du fil qui a le clavier dans son app ; 0 sans fenêtre. La barre lit celle de l'app
// qu'elle suit (son propre menu peut avoir le premier plan).
std::uint32_t windowLayout(HWND window);
// Demande à cette fenêtre (à celle qui a son clavier) de passer à la disposition (WM_INPUTLANGCHANGEREQUEST, comme la
// barre de langue de Windows) ; false sans fenêtre ou si la disposition n'est pas installée.
bool switchWindowLayout(HWND window, std::uint32_t hkl);

std::wstring hklText(std::uint32_t hkl);           // « 040C040C »
std::uint32_t hklFromText(std::wstring_view text);   // 0 si illisible

} // namespace md
