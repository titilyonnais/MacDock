#include "status_input.h"

#include <windows.h>
#include <imm.h>

#include <cwchar>

#pragma comment(lib, "imm32.lib")   // ImmGetDefaultIMEWnd : fenêtre IME des consoles

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

// Fenêtre qui a le clavier dans le fil de w (sinon w elle-même).
HWND keyboardWindow(HWND w) {
    if (!w || !IsWindow(w)) return nullptr;
    GUITHREADINFO gi{sizeof gi};
    if (GetGUIThreadInfo(GetWindowThreadProcessId(w, nullptr), &gi) && gi.hwndFocus) return gi.hwndFocus;
    return w;
}

constexpr wchar_t kLayoutsKey[] = L"SYSTEM\\CurrentControlSet\\Control\\Keyboard Layouts";

// Clé de la variante dont « Layout Id » vaut id (« 00CF » : BÉPO), parmi les claviers du registre ; vide sinon.
std::wstring registryVariantKlid(unsigned short id) {
    HKEY root = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, kLayoutsKey, 0, KEY_READ, &root) != ERROR_SUCCESS) return {};
    std::wstring found;
    for (DWORD i = 0; found.empty(); ++i) {
        wchar_t name[256] = {};
        DWORD len = 256;
        const LSTATUS r = RegEnumKeyExW(root, i, name, &len, nullptr, nullptr, nullptr, nullptr);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS) continue;
        wchar_t value[16] = {};
        DWORD size = sizeof value;
        if (RegGetValueW(root, name, L"Layout Id", RRF_RT_REG_SZ, nullptr, value, &size) == ERROR_SUCCESS &&
            value[0] && std::wcstoul(value, nullptr, 16) == id)
            found = name;
    }
    RegCloseKey(root);
    return found;
}

// Nom du clavier dans la langue de Windows (« Français (Standard, BÉPO) ») ; à défaut son nom anglais ; vide si inconnu.
std::wstring keyboardName(std::uint32_t hkl) {
    const std::wstring klid = layoutKlid(hkl, registryVariantKlid);
    if (klid.empty()) return {};
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (std::wstring(kLayoutsKey) + L"\\" + klid).c_str(), 0, KEY_READ, &key) != ERROR_SUCCESS)
        return {};
    wchar_t out[256] = {};
    std::wstring name;
    if (RegLoadMUIStringW(key, L"Layout Display Name", out, sizeof out, nullptr, 0, nullptr) == ERROR_SUCCESS) name = out;
    DWORD size = sizeof out;
    if (name.empty() && RegGetValueW(key, nullptr, L"Layout Text", RRF_RT_REG_SZ, nullptr, out, &size) == ERROR_SUCCESS)
        name = out;
    RegCloseKey(key);
    return name;
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

std::wstring layoutKlid(std::uint32_t hkl, const std::function<std::wstring(unsigned short)>& variantKlid) {
    if (!hkl) return {};
    const auto device = static_cast<unsigned short>(hkl >> 16), lang = static_cast<unsigned short>(hkl & 0xFFFF);
    if ((device & 0xF000) == 0xE000) return hklText(hkl);   // IME : sa clé est le HKL entier
    if ((device & 0xF000) == 0xF000) return variantKlid ? variantKlid(static_cast<unsigned short>(device & 0x0FFF)) : std::wstring();
    wchar_t out[16] = {};
    std::swprintf(out, 16, L"0000%04X", unsigned(device ? device : lang));
    return out;
}

std::wstring layoutFullName(std::uint32_t hkl) {
    std::wstring lang = localeInfo(static_cast<unsigned short>(hkl & 0xFFFF), LOCALE_SLOCALIZEDDISPLAYNAME);
    if (lang.empty()) lang = layoutName(hkl);
    std::wstring keyboard = keyboardName(hkl);
    if (keyboard.empty()) keyboard = hklText(hkl);
    // Clavier du même nom que sa langue (« Anglais (États-Unis) ») : le nom seul suffit, l'autre ligne a son clavier.
    return keyboard == lang ? lang : lang + L" – " + keyboard;
}

void distinguishLayouts(std::vector<InputLayout>& list, const std::function<std::wstring(std::uint32_t)>& fullName) {
    if (!fullName) return;
    std::vector<bool> twin(list.size(), false);
    for (std::size_t i = 0; i < list.size(); ++i)
        for (std::size_t j = i + 1; j < list.size(); ++j)
            if (list[i].name == list[j].name) twin[i] = twin[j] = true;
    for (std::size_t i = 0; i < list.size(); ++i)
        if (twin[i]) list[i].name = fullName(list[i].hkl);
}

HWND layoutWindow(HWND window, std::wstring_view className, const std::function<HWND(HWND)>& imeWindow) {
    if (className != L"ConsoleWindowClass" && className != L"PseudoConsoleWindow") return window;
    const HWND ime = imeWindow ? imeWindow(window) : nullptr;
    return ime ? ime : window;
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
    distinguishLayouts(out);
    return out;
}

std::uint32_t windowLayout(HWND window) {
    HWND w = keyboardWindow(window);
    if (!w) return 0;
    wchar_t cls[64] = {};
    GetClassNameW(w, cls, 64);
    w = layoutWindow(w, cls, [](HWND h) { return ImmGetDefaultIMEWnd(h); });
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(GetKeyboardLayout(GetWindowThreadProcessId(w, nullptr))));
}

bool switchWindowLayout(HWND window, std::uint32_t hkl) {
    const int n = GetKeyboardLayoutList(0, nullptr);
    if (n <= 0 || !hkl) return false;
    std::vector<HKL> list(std::size_t(n), nullptr);
    const int got = GetKeyboardLayoutList(n, list.data());
    HKL target = nullptr;   // le vrai HKL (64 bits) de cette disposition
    for (int i = 0; i < got; ++i)
        if (static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(list[std::size_t(i)])) == hkl) target = list[std::size_t(i)];
    const HWND w = keyboardWindow(window);
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
