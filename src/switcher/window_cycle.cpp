#include "window_cycle.h"

#include "../core/strings.h"

namespace md {

bool isCycleKey(unsigned scanCode, bool extended) { return scanCode == 0x29 && !extended; }

CycleKey cycleKeyAction(const CycleKeyEvent& e) {
    if (!e.down) return e.taken ? CycleKey::Swallow : CycleKey::Pass;   // relâchement : avalé s'il suit un appui pris
    if (e.injected || !e.alt || e.ctrl || e.win) return CycleKey::Pass;    // Alt Gr (Ctrl+Alt), ⊞ : pas pour nous
    if (e.repeat) return CycleKey::Swallow;                                // appui tenu : une fenêtre par appui
    if (e.session) return e.shift ? CycleKey::SwitcherNext : CycleKey::SwitcherBack;
    return e.shift ? CycleKey::Prev : CycleKey::Next;
}

std::optional<std::size_t> cycleTarget(const std::vector<bool>& eligible, std::optional<std::size_t> current, bool back) {
    const std::size_t n = eligible.size();
    if (!current || *current >= n) {   // premier plan hors de la liste : la première (ou la dernière) qui compte
        for (std::size_t k = 0; k < n; ++k) {
            const std::size_t i = back ? n - 1 - k : k;
            if (eligible[i]) return i;
        }
        return std::nullopt;
    }
    for (std::size_t k = 1; k < n; ++k) {   // en boucle, sans revenir sur current
        const std::size_t i = back ? (*current + n - k) % n : (*current + k) % n;
        if (eligible[i]) return i;
    }
    return std::nullopt;
}

bool parseWindowCycleHotkey(const std::wstring& text) {
    std::wstring t;
    for (wchar_t c : toLower(text))
        if (c != L' ') t.push_back(c);
    return t == L"alt+backtick";
}

} // namespace md
