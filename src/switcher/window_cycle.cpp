#include "window_cycle.h"

#include "../core/strings.h"

namespace md {

bool isCycleKey(unsigned scanCode, bool extended) { return scanCode == 0x29 && !extended; }

CycleKey cycleKeyAction(const CycleKeyEvent& e) {
    // Suite d'un appui pris (répétition, relâchement) : avalée, même si Alt a été lâché avant la touche.
    if (e.taken && (!e.down || e.repeat)) return CycleKey::Swallow;
    if (!e.down) return CycleKey::Pass;
    if (e.injected || !e.alt || e.ctrl || e.win) return CycleKey::Pass;    // Alt Gr (Ctrl+Alt), ⊞ : pas pour nous
    if (e.repeat) return CycleKey::Swallow;                                // appui tenu : une fenêtre par appui
    if (e.session) return e.shift ? CycleKey::SwitcherRight : CycleKey::SwitcherLeft;
    return e.shift ? CycleKey::Prev : CycleKey::Next;
}

CycleStep CycleKeyGate::onKey(CycleKeyEvent e) {
    e.taken = taken_;
    CycleStep s{cycleKeyAction(e), false};
    if (!e.down) taken_ = false;
    switch (s.action) {
        case CycleKey::Next:
        case CycleKey::Prev:
        case CycleKey::SwitcherLeft:
        case CycleKey::SwitcherRight:
            taken_ = true;      // son relâchement sera avalé aussi
            s.neutral = true;   // tout de suite, par le crochet : Alt relâché ensuite n'ouvre pas le menu de l'app
            break;
        default: break;
    }
    return s;
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
