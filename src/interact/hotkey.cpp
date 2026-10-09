#include "hotkey.h"

#include <cwctype>
#include <vector>

namespace md {

namespace {

struct KeyName {
    const wchar_t* name;
    UINT vk;
    const wchar_t* label;
};

// Touches nommées (le texte des réglages) et leur symbole, comme dans les menus de macOS.
constexpr KeyName kKeys[] = {
    {L"space", VK_SPACE, L"Espace"},   {L"tab", VK_TAB, L"⇥"},          {L"enter", VK_RETURN, L"↩"},
    {L"escape", VK_ESCAPE, L"⎋"},      {L"up", VK_UP, L"↑"},            {L"down", VK_DOWN, L"↓"},
    {L"left", VK_LEFT, L"←"},          {L"right", VK_RIGHT, L"→"},      {L"home", VK_HOME, L"↖"},
    {L"end", VK_END, L"↘"},            {L"pageup", VK_PRIOR, L"⇞"},     {L"pagedown", VK_NEXT, L"⇟"},
    {L"insert", VK_INSERT, L"Inser"},  {L"delete", VK_DELETE, L"⌦"},    {L"comma", VK_OEM_COMMA, L","},
    {L"period", VK_OEM_PERIOD, L"."},  {L"minus", VK_OEM_MINUS, L"-"},  {L"plus", VK_OEM_PLUS, L"+"},
};

std::optional<UINT> modifierOf(const std::wstring& t) {
    if (t == L"ctrl" || t == L"control") return UINT(MOD_CONTROL);
    if (t == L"alt") return UINT(MOD_ALT);
    if (t == L"shift" || t == L"maj") return UINT(MOD_SHIFT);
    if (t == L"win") return UINT(MOD_WIN);
    return std::nullopt;
}

std::optional<UINT> keyOf(const std::wstring& t) {
    if (t.size() == 1 && ((t[0] >= L'a' && t[0] <= L'z') || (t[0] >= L'0' && t[0] <= L'9'))) return UINT(std::towupper(t[0]));
    if (t.size() >= 2 && t[0] == L'f') {
        int n = 0;
        for (std::size_t i = 1; i < t.size(); ++i) {
            if (t[i] < L'0' || t[i] > L'9') return std::nullopt;
            n = n * 10 + (t[i] - L'0');
        }
        if (n >= 1 && n <= 24) return UINT(VK_F1 + n - 1);
        return std::nullopt;
    }
    for (const KeyName& k : kKeys)
        if (t == k.name) return k.vk;
    return std::nullopt;
}

bool isFunctionKey(UINT vk) { return vk >= VK_F1 && vk <= VK_F24; }

}  // namespace

std::optional<HotkeySpec> parseHotkey(std::wstring_view text, bool allowReserved) {
    std::vector<std::wstring> parts(1);
    for (wchar_t c : text) {
        if (c == L' ') continue;
        if (c == L'+') parts.emplace_back();
        else parts.back().push_back(wchar_t(std::towlower(c)));
    }
    HotkeySpec s;
    bool haveKey = false;
    for (const auto& p : parts) {
        if (p.empty()) return std::nullopt;   // « ctrl+ », « ++ »
        if (auto m = modifierOf(p)) {
            s.mods |= *m;
        } else if (auto k = keyOf(p); k && !haveKey) {
            s.vk = *k;
            haveKey = true;
        } else {
            return std::nullopt;   // inconnu, ou deux touches
        }
    }
    if (!haveKey || (!s.mods && !isFunctionKey(s.vk))) return std::nullopt;
    if (!allowReserved && hotkeyReserved(s)) return std::nullopt;
    return s;
}

HotkeyRecord recordHotkey(UINT vk, UINT mods) {
    switch (vk) {
        case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL:
        case VK_MENU: case VK_LMENU: case VK_RMENU:
        case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT:
        case VK_LWIN: case VK_RWIN:
            return {};
        default: break;
    }
    if (!mods && vk == VK_ESCAPE) return {RecordKind::Cancel, {}};
    if (!mods && (vk == VK_BACK || vk == VK_DELETE)) return {RecordKind::Clear, {}};
    const HotkeySpec s{mods, vk};
    // Seulement les touches que les réglages savent écrire (et relire à l'identique).
    const auto back = parseHotkey(hotkeyText(s), true);
    if (!back || !(*back == s)) return {};
    if (hotkeyReserved(s)) return {RecordKind::Reserved, s};
    if (mods && !(mods & (MOD_ALT | MOD_WIN))) return {RecordKind::Common, s};   // Ctrl+C, Maj+→… : aux apps
    return {RecordKind::Accept, s};
}

bool KeyGate::swallow(UINT vk, bool down) {
    const std::size_t k = vk & 0xFF;
    if (down) {
        if (listening_) swallowed_.set(k);
        return swallowed_.test(k);   // répétitions d'une touche gardée comprises
    }
    if (!swallowed_.test(k)) return false;
    swallowed_.reset(k);
    return true;
}

bool hotkeyReserved(const HotkeySpec& s) {
    const HotkeySpec reserved[] = {{MOD_WIN, 'L'},
                                   {MOD_CONTROL | MOD_ALT, VK_DELETE},
                                   {MOD_ALT, VK_TAB},
                                   {MOD_ALT, VK_F4},
                                   {MOD_CONTROL | MOD_SHIFT, VK_ESCAPE},
                                   {MOD_WIN, VK_SPACE},    // disposition du clavier
                                   // MacDock lui-même : calque de diagnostic, opacité du Dock (dock_window.cpp)
                                   {MOD_CONTROL | MOD_ALT | MOD_SHIFT, 'O'},
                                   {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_UP},
                                   {MOD_CONTROL | MOD_ALT | MOD_SHIFT, VK_DOWN}};
    for (const auto& r : reserved)
        if (r == s) return true;
    return false;
}

std::wstring hotkeyText(const HotkeySpec& s) {
    std::wstring out;
    if (s.mods & MOD_CONTROL) out += L"ctrl+";
    if (s.mods & MOD_ALT) out += L"alt+";
    if (s.mods & MOD_SHIFT) out += L"shift+";
    if (s.mods & MOD_WIN) out += L"win+";
    if ((s.vk >= 'A' && s.vk <= 'Z') || (s.vk >= '0' && s.vk <= '9')) return out + wchar_t(std::towlower(wchar_t(s.vk)));
    if (isFunctionKey(s.vk)) return out + L"f" + std::to_wstring(s.vk - VK_F1 + 1);
    for (const KeyName& k : kKeys)
        if (k.vk == s.vk) return out + k.name;
    return out + L"?";
}

std::wstring hotkeyLabel(const HotkeySpec& s) {
    std::wstring out;
    if (s.mods & MOD_CONTROL) out += L"⌃";
    if (s.mods & MOD_ALT) out += L"⌥";
    if (s.mods & MOD_SHIFT) out += L"⇧";
    if (s.mods & MOD_WIN) out += L"⊞";
    if ((s.vk >= 'A' && s.vk <= 'Z') || (s.vk >= '0' && s.vk <= '9')) return out + wchar_t(s.vk);
    if (isFunctionKey(s.vk)) return out + L"F" + std::to_wstring(s.vk - VK_F1 + 1);
    for (const KeyName& k : kKeys)
        if (k.vk == s.vk) return out + k.label;
    return out + L"?";
}

bool hotkeyNeedsRegister(const HotkeySlot& slot, const std::wstring& setting, bool parsable) {
    if (!slot.tried || setting != slot.applied) return true;
    return parsable && !slot.registered;
}

bool hotkeyConflict(std::wstring_view a, std::wstring_view b) {
    const auto x = parseHotkey(a, true), y = parseHotkey(b, true);
    return x && y && *x == *y;
}

}  // namespace md
