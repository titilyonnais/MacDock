// Fenêtre suivante de l'app au clavier (plan 59, logique pure) : ⌘` de macOS, « Déplacer la cible vers la fenêtre
// suivante ». Alt + la touche sous Échap fait passer devant la fenêtre suivante de l'app au premier plan.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace md {

// La touche sous Échap (au-dessus de Tab), reconnue à sa place et non à son caractère : ` en QWERTY, ² en AZERTY,
// ^ en QWERTZ (code de balayage 0x29, non étendu).
bool isCycleKey(unsigned scanCode, bool extended);

// Frappe de cette touche, vue par le crochet clavier du Dock.
struct CycleKeyEvent {
    bool down = true, repeat = false;
    bool alt = false, ctrl = false, win = false, shift = false;
    bool injected = false;   // simulée (acceptée en diagnostic : l'appelant le décide)
    bool taken = false;      // l'appui a été pris : son relâchement est avalé aussi
    bool session = false;    // sélecteur d'apps ouvert (⌥⇥ tenu)
};
// Pass : laissée à l'app ; Swallow : avalée sans effet ; Next / Prev : fenêtre suivante ou précédente ;
// SwitcherBack / SwitcherNext : une case du sélecteur en arrière ou en avant (comme ⌘` pendant ⌘⇥).
enum class CycleKey { Pass, Swallow, Next, Prev, SwitcherBack, SwitcherNext };
CycleKey cycleKeyAction(const CycleKeyEvent& e);

// Fenêtre à faire passer devant parmi les fenêtres de l'app (ordre du Dock) : la suivante (ou la précédente) après
// current, en boucle, parmi celles qui comptent (eligible : visibles, non réduites, sur ce bureau). current absent ou
// hors liste : la première (ou la dernière) qui compte. nullopt s'il n'y en a pas d'autre que current.
std::optional<std::size_t> cycleTarget(const std::vector<bool>& eligible, std::optional<std::size_t> current, bool back);

// « alt+backtick » (casse et espaces ignorées) : actif ; « off » ou autre chose : non.
bool parseWindowCycleHotkey(const std::wstring& text);

} // namespace md
