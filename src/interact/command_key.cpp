#include "command_key.h"

#include <windows.h>

namespace md {

std::optional<Chord> commandChord(unsigned vk, bool shift, bool explorer) {
    if (explorer) {
        if (vk == VK_UP) return Chord{VK_UP, false, false, true};   // ⌘↑ : dossier parent
        if (vk == VK_DOWN) return Chord{VK_RETURN, false, false, false};   // ⌘↓ : ouvrir
        if (vk == VK_BACK) return Chord{VK_DELETE, false, false, false};   // ⌘⌫ : à la Corbeille (annulable)
    }
    switch (vk) {
        case 'A': case 'C': case 'V': case 'X': case 'Z': case 'S': case 'W':
        case 'T': case 'N': case 'F': case 'P': case 'O': case 'R': case 'Y':
        case VK_OEM_COMMA:   // ⌘, : réglages de l'app (Ctrl+, chez beaucoup)
            return Chord{vk, true, shift, false};
        case 'Q': return Chord{VK_F4, false, false, true};   // quitter : l'app demande d'enregistrer si besoin
        case VK_LEFT: return Chord{VK_HOME, false, shift, false};   // début de ligne
        case VK_RIGHT: return Chord{VK_END, false, shift, false};   // fin de ligne
        case VK_UP: return Chord{VK_HOME, true, shift, false};      // début du document
        case VK_DOWN: return Chord{VK_END, true, shift, false};     // fin du document
        default: return std::nullopt;
    }
}

CommandAction CommandKeys::onKey(unsigned vk, bool down, bool shift, bool explorer) {
    CommandAction pass;
    CommandAction swallow;
    swallow.kind = CommandAction::Kind::Swallow;
    if (vk == VK_LMENU) {
        if (realAlt_) {   // Alt rendu à Windows : son relâchement aussi
            if (!down) realAlt_ = held_ = false;
            return pass;
        }
        held_ = down;
        return swallow;   // ⌘ seul ne fait rien (pas de menu de l'app), comme sur macOS
    }
    if (vk == VK_RMENU || !held_ || realAlt_) return pass;   // Alt Gr, pas de ⌘, ou Alt déjà réel
    switch (vk) {
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_LWIN: case VK_RWIN:
            return pass;   // modificateurs : ⌘⇧Z…
        default: break;
    }
    if (const auto chord = commandChord(vk, shift, explorer)) {
        if (!down) return swallow;
        CommandAction send;
        send.kind = CommandAction::Kind::Send;
        send.chord = *chord;
        return send;
    }
    if (!down) return pass;
    realAlt_ = true;   // Alt+Tab, Alt+F4, Alt+Entrée : un vrai Alt, jusqu'au relâchement
    CommandAction alt;
    alt.kind = CommandAction::Kind::AltThenPass;
    return alt;
}

} // namespace md
