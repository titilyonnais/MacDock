#include "status_input.h"

#include <windows.h>

#include <cwchar>

namespace md {

namespace {

// Langue qui nomme la disposition : son mot haut quand c'est une langue (« 040C » : français), sinon sa langue (mot
// bas) — les dispositions variantes (Dvorak…) ont un mot haut en 0xF….
unsigned short layoutLang(std::uint32_t hkl) {
    const auto device = static_cast<unsigned short>(hkl >> 16), lang = static_cast<unsigned short>(hkl & 0xFFFF);
    return device != 0 && (device & 0xF000) == 0 ? device : lang;
}

std::wstring localeName(unsigned short langid) {
    wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
    if (!langid || !LCIDToLocaleName(MAKELCID(langid, SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) return {};
    return name;
}

std::wstring localeInfo(unsigned short langid, LCTYPE what) {
    const std::wstring name = localeName(langid);
    wchar_t out[128] = {};
    if (name.empty() || !GetLocaleInfoEx(name.c_str(), what, out, 128)) return {};
    return out;
}

// Fenêtre qui a le clavier dans l'app au premier plan (sinon la fenêtre au premier plan elle-même).
HWND keyboardWindow() {
    const HWND fg = GetForegroundWindow();
    if (!fg) return nullptr;
    GUITHREADINFO gi{sizeof gi};
    if (GetGUIThreadInfo(GetWindowThreadProcessId(fg, nullptr), &gi) && gi.hwndFocus) return gi.hwndFocus;
    return fg;
}

} // namespace

std::wstring layoutCountry(unsigned short langid) { return localeInfo(langid, LOCALE_SISO3166CTRYNAME); }

std::wstring layoutCode(std::uint32_t hkl, const std::function<std::wstring(unsigned short)>& country) {
    if (!hkl || !country) return {};
    std::wstring code = country(layoutLang(hkl));
    if (code.empty()) code = country(static_cast<unsigned short>(hkl & 0xFFFF));   // disposition inconnue : sa langue
    return code;
}

std::wstring layoutName(std::uint32_t hkl) {
    std::wstring name = localeInfo(layoutLang(hkl), LOCALE_SLOCALIZEDDISPLAYNAME);
    return name.empty() ? hklText(hkl) : name;
}

std::vector<InputLayout> installedLayouts() {
    std::vector<InputLayout> out;
    const int n = GetKeyboardLayoutList(0, nullptr);
    if (n <= 0) return out;
    std::vector<HKL> list(std::size_t(n), nullptr);
    const int got = GetKeyboardLayoutList(n, list.data());
    for (int i = 0; i < got; ++i) {
        const auto hkl = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(list[std::size_t(i)]));
        out.push_back({hkl, layoutName(hkl), layoutCode(hkl)});
    }
    return out;
}

std::uint32_t foregroundLayout() {
    const HWND w = keyboardWindow();
    if (!w) return 0;
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(GetWindowThreadProcessId(w, nullptr))));
}

bool switchForegroundLayout(std::uint32_t hkl) {
    const int n = GetKeyboardLayoutList(0, nullptr);
    if (n <= 0 || !hkl) return false;
    std::vector<HKL> list(std::size_t(n), nullptr);
    const int got = GetKeyboardLayoutList(n, list.data());
    HKL target = nullptr;   // le vrai HKL (64 bits) de cette disposition
    for (int i = 0; i < got; ++i)
        if (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(list[std::size_t(i)])) == hkl) target = list[std::size_t(i)];
    const HWND w = keyboardWindow();
    if (!target || !w) return false;
    // Comme la barre de langue de Windows : la fenêtre demande elle-même le changement (DefWindowProc).
    return PostMessageW(w, WM_INPUTLANGCHANGEREQUEST, 0, reinterpret_cast<LPARAM>(target)) != FALSE;
}

std::wstring hklText(std::uint32_t hkl) {
    wchar_t out[16] = {};
    std::swprintf(out, 16, L"%08X", static_cast<unsigned>(hkl));
    return out;
}

std::uint32_t hklFromText(std::wstring_view text) {
    if (text.empty() || text.size() > 8) return 0;
    std::uint32_t v = 0;
    for (wchar_t c : text) {
        unsigned d;
        if (c >= L'0' && c <= L'9') d = unsigned(c - L'0');
        else if (c >= L'A' && c <= L'F') d = unsigned(c - L'A' + 10);
        else if (c >= L'a' && c <= L'f') d = unsigned(c - L'a' + 10);
        else return 0;
        v = v * 16 + d;
    }
    return v;
}

} // namespace md
