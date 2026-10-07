#include "shortcut.h"

#include <string>

namespace md {
namespace {

std::optional<WORD> modifierKey(const std::wstring& t) {
    if (t == L"ctrl") return WORD(VK_CONTROL);
    if (t == L"maj") return WORD(VK_SHIFT);
    if (t == L"alt") return WORD(VK_MENU);
    if (t == L"win") return WORD(VK_LWIN);
    return std::nullopt;
}

std::optional<WORD> mainKey(const std::wstring& t) {
    if (t.size() == 1) {
        wchar_t c = t[0];
        if (c >= L'a' && c <= L'z') return WORD(c - L'a' + L'A');
        if (c >= L'0' && c <= L'9') return WORD(c);
        switch (c) {
            case L',': return WORD(VK_OEM_COMMA);
            case L'.': return WORD(VK_OEM_PERIOD);
            case L'←': return WORD(VK_LEFT);
            case L'→': return WORD(VK_RIGHT);
            case L'↑': return WORD(VK_UP);
            case L'↓': return WORD(VK_DOWN);
            default: return std::nullopt;
        }
    }
    if (t == L"fin") return WORD(VK_END);   // avant les touches de fonction (F + chiffres)
    if (t.size() >= 2 && t.size() <= 3 && t[0] == L'f') {
        int n = 0;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] < L'0' || t[i] > L'9') return std::nullopt;
            n = n * 10 + int(t[i] - L'0');
        }
        if (n >= 1 && n <= 24) return WORD(VK_F1 + n - 1);
        return std::nullopt;
    }
    if (t == L"échap" || t == L"echap") return WORD(VK_ESCAPE);
    if (t == L"entrée" || t == L"entree") return WORD(VK_RETURN);
    if (t == L"suppr") return WORD(VK_DELETE);
    if (t == L"tab") return WORD(VK_TAB);
    if (t == L"espace") return WORD(VK_SPACE);
    if (t == L"plus") return WORD(VK_ADD);        // pavé numérique : même effet quelle que soit la disposition
    if (t == L"moins") return WORD(VK_SUBTRACT);
    if (t == L"origine") return WORD(VK_HOME);
    if (t == L"pg.préc" || t == L"pg.prec") return WORD(VK_PRIOR);
    if (t == L"pg.suiv") return WORD(VK_NEXT);
    return std::nullopt;
}

bool extended(WORD vk) {
    switch (vk) {
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_LWIN: case VK_RWIN: case VK_DELETE: case VK_INSERT:
        case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT: return true;
        default: return false;
    }
}

INPUT keyInput(WORD vk, bool up) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.dwFlags = (up ? KEYEVENTF_KEYUP : 0u) | (extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0u);
    return in;
}

} // namespace

std::optional<Shortcut> parseShortcut(std::wstring_view text) {
    if (text.empty()) return std::nullopt;
    Shortcut s;
    std::wstring lower(text);   // CharLowerBuffW : « É » aussi (towlower ne connaît que l'ASCII en locale « C »)
    CharLowerBuffW(lower.data(), DWORD(lower.size()));
    std::size_t start = 0;
    bool haveKey = false;
    while (start <= lower.size()) {
        std::size_t plus = lower.find(L'+', start);
        std::wstring part = lower.substr(start, plus == std::wstring::npos ? std::wstring::npos : plus - start);
        if (part.empty() || haveKey) return std::nullopt;   // « Ctrl+ », « ++ », ou quelque chose après la touche
        if (auto m = modifierKey(part)) {
            s.modifiers.push_back(*m);
        } else if (auto k = mainKey(part)) {
            s.key = *k;
            haveKey = true;
        } else {
            return std::nullopt;
        }
        if (plus == std::wstring::npos) break;
        start = plus + 1;
    }
    if (!haveKey) return std::nullopt;
    return s;
}

std::vector<INPUT> shortcutInputs(const Shortcut& s) {
    std::vector<INPUT> out;
    for (WORD m : s.modifiers) out.push_back(keyInput(m, false));
    out.push_back(keyInput(s.key, false));
    out.push_back(keyInput(s.key, true));
    for (auto it = s.modifiers.rbegin(); it != s.modifiers.rend(); ++it) out.push_back(keyInput(*it, true));
    return out;
}

} // namespace md
