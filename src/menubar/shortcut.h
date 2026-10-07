// Raccourcis clavier des menus de la barre : analyse du texte affiché et séquence SendInput (logique pure).
#pragma once
#include <windows.h>

#include <optional>
#include <string_view>
#include <vector>

namespace md {

struct Shortcut {
    std::vector<WORD> modifiers;   // VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, dans l'ordre du texte
    WORD key = 0;
};

// « Ctrl+Maj+S », « Alt+F4 », « Win+L », « F11 », « Ctrl+Plus », « Ctrl+, », « Alt+← », « Ctrl+Maj+Échap ».
// Casse indifférente ; nullopt si un élément est inconnu ou s'il n'y a pas de touche.
std::optional<Shortcut> parseShortcut(std::wstring_view text);
// Appuis (modificateurs puis touche), puis relâchements en ordre inverse ; touches étendues marquées.
std::vector<INPUT> shortcutInputs(const Shortcut& s);

} // namespace md
