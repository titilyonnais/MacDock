// Raccourcis globaux (Spotlight, Mission Control, Exposé…) saisis librement dans l'app Réglages (logique pure) :
// analyse du texte des réglages (« ctrl+alt+up »), texte, symboles macOS (« ⌃⌥↑ »), combinaisons réservées par Windows.
#pragma once
#include <windows.h>

#include <bitset>
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
// Win+L, Ctrl+Alt+Suppr, Alt+Tab (le sélecteur d'apps), Alt+F4, Ctrl+Maj+Échap, Win+Espace : Windows les garde ;
// Ctrl+Alt+Maj+O, ↑ et ↓ : MacDock lui-même (calque de diagnostic, opacité du Dock).
bool hotkeyReserved(const HotkeySpec& s);
std::wstring hotkeyText(const HotkeySpec& s);    // « ctrl+alt+up » (format des réglages)
std::wstring hotkeyLabel(const HotkeySpec& s);   // « ⌃⌥↑ », dans l'ordre de macOS ; ⊞ pour Windows
// Deux réglages désignent-ils la même combinaison (casse, espaces et ordre ignorés) ? Jamais pour « off ».
bool hotkeyConflict(std::wstring_view a, std::wstring_view b);

// Raccourci global enregistré par le Dock (RegisterHotKey) : réglage du dernier essai, et s'il a été accepté.
struct HotkeySlot {
    std::wstring applied;
    bool registered = false;
    bool tried = false;
    bool refused = false;   // RegisterHotKey a échoué : réessayé
};
// Faut-il (ré)essayer ? Réglage changé, ou dernier essai refusé (raccourci pris par une autre app au démarrage :
// réessayé jusqu'à ce qu'elle le libère). `parsable` faux : raccourci désactivé, rien à enregistrer.
bool hotkeyNeedsRegister(const HotkeySlot& slot, const std::wstring& setting, bool parsable);

// Enregistreur de raccourci (app Réglages) : une touche appuyée, avec les modificateurs tenus (MOD_…).
//  - Wait : modificateur seul, touche sans nom, ou touche ordinaire sans modificateur (on continue d'écouter) ;
//  - Accept : `spec` est le nouveau raccourci ; Reserved : Windows (ou MacDock lui-même) garde cette combinaison ;
//  - Common : Ctrl ou Maj sans Alt ni ⊞, les raccourcis des apps (Ctrl+C…) : refusé ;
//  - Clear : Retour arrière ou Suppr, « Aucun » ; Cancel : Échap.
enum class RecordKind { Wait, Accept, Reserved, Common, Clear, Cancel };
struct HotkeyRecord {
    RecordKind kind = RecordKind::Wait;
    HotkeySpec spec;
};
HotkeyRecord recordHotkey(UINT vk, UINT mods);

// Crochet de l'enregistreur : quelles touches garder pour lui. Pendant l'écoute, tout appui est gardé (et retenu) ; une
// relâche ne l'est que si son appui l'a été, pour que les touches tenues avant l'écoute (le Maj d'un Maj+Entrée) soient
// relâchées normalement. Après l'écoute, les appuis gardés le restent jusqu'à leur relâche (`pending`), sinon Windows
// verrait une touche relâchée sans l'avoir vue enfoncée.
class KeyGate {
public:
    void listen(bool on) { listening_ = on; }
    bool swallow(UINT vk, bool down);   // vrai : la touche n'atteint pas le système
    bool pending() const { return swallowed_.any(); }
    void reset() { swallowed_.reset(); }

private:
    bool listening_ = false;
    std::bitset<256> swallowed_;
};

}  // namespace md
