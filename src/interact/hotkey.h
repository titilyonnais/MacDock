// Raccourcis globaux (Spotlight, Mission Control, Exposé…) saisis librement dans l'app Réglages (logique pure) :
// analyse du texte des réglages (« ctrl+alt+up »), texte, symboles macOS (« ⌃⌥↑ »), combinaisons réservées par Windows.
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace md {

struct HotkeySpec {
    UINT mods = 0;   // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN (pour RegisterHotKey)
    UINT vk = 0;
    bool operator==(const HotkeySpec&) const = default;
};

// « ctrl+alt+shift+win+<touche> », casse et espaces ignorés : lettres, chiffres, f1 à f24, space, tab, enter, escape,
// flèches, home, end, pageup, pagedown, insert, delete, comma, period, minus, plus. Au moins un modificateur, sauf pour
// une touche F. nullopt pour « off », une valeur inconnue, ou une combinaison réservée par Windows (sauf allowReserved).
std::optional<HotkeySpec> parseHotkey(std::wstring_view text, bool allowReserved = false);
// Win+L, Ctrl+Alt+Suppr, Alt+Tab (le sélecteur d'apps), Alt+F4, Ctrl+Maj+Échap, Win+Espace : Windows les garde.
bool hotkeyReserved(const HotkeySpec& s);
std::wstring hotkeyText(const HotkeySpec& s);    // « ctrl+alt+up » (format des réglages)
std::wstring hotkeyLabel(const HotkeySpec& s);   // « ⌃⌥↑ », dans l'ordre de macOS ; ⊞ pour Windows
// Deux réglages désignent-ils la même combinaison (casse, espaces et ordre ignorés) ? Jamais pour « off ».
bool hotkeyConflict(std::wstring_view a, std::wstring_view b);

// Enregistreur de raccourci (app Réglages) : une touche appuyée, avec les modificateurs tenus (MOD_…).
//  - Wait : modificateur seul, touche sans nom, ou touche ordinaire sans modificateur (on continue d'écouter) ;
//  - Accept : `spec` est le nouveau raccourci ; Reserved : Windows garde cette combinaison ;
//  - Clear : Retour arrière ou Suppr, « Aucun » ; Cancel : Échap.
enum class RecordKind { Wait, Accept, Reserved, Clear, Cancel };
struct HotkeyRecord {
    RecordKind kind = RecordKind::Wait;
    HotkeySpec spec;
};
HotkeyRecord recordHotkey(UINT vk, UINT mods);

}  // namespace md
