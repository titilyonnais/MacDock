#include "mods.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace md {

const std::vector<ModInfo>& macdockMods() {
    static const std::vector<ModInfo> mods = {
        {L"macdock-look", L"Police macOS (SF Pro)", L"SF Pro dans toutes les apps, à la place de Segoe UI",
         L"install-macdock-look.ps1"},
    };
    return mods;
}

std::optional<std::wstring> sourceVersion(std::string_view source) {
    const std::size_t at = source.find("@version");
    if (at == std::string_view::npos) return std::nullopt;
    std::size_t i = at + 8;
    while (i < source.size() && (source[i] == ' ' || source[i] == '\t')) ++i;
    std::wstring v;
    while (i < source.size() && ((source[i] >= '0' && source[i] <= '9') || source[i] == '.')) v.push_back(wchar_t(source[i++]));
    if (v.empty()) return std::nullopt;
    return v;
}

int compareVersions(std::wstring_view a, std::wstring_view b) {
    auto next = [](std::wstring_view& s) {
        int n = 0;
        while (!s.empty() && s.front() >= L'0' && s.front() <= L'9') {
            n = n * 10 + (s.front() - L'0');
            s.remove_prefix(1);
        }
        if (!s.empty() && s.front() == L'.') s.remove_prefix(1);
        return n;
    };
    while (!a.empty() || !b.empty()) {
        const int x = next(a), y = next(b);
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

std::wstring modSourcePath(const std::wstring& exeDir, const std::wstring& id) {
    for (const wchar_t* rel : {L"\\windhawk\\", L"\\..\\..\\windhawk\\"}) {
        const std::wstring p = exeDir + rel + id + L".wh.cpp";
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    }
    return {};
}

std::optional<std::wstring> modSourceVersion(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::string head(4096, '\0');
    f.read(head.data(), std::streamsize(head.size()));
    head.resize(std::size_t(f.gcount()));
    return sourceVersion(head);
}

bool windhawkInstalled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Windhawk", 0, KEY_READ, &key) != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

std::optional<InstalledMod> installedMod(const std::wstring& id) {
    const std::wstring path = L"SOFTWARE\\Windhawk\\Engine\\Mods\\local@" + id;
    wchar_t version[64] = {};
    DWORD size = sizeof version;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"Version", RRF_RT_REG_SZ, nullptr, version, &size) != ERROR_SUCCESS)
        return std::nullopt;
    InstalledMod m;
    m.version = version;
    DWORD disabled = 0;
    size = sizeof disabled;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, path.c_str(), L"Disabled", RRF_RT_REG_DWORD, nullptr, &disabled, &size) == ERROR_SUCCESS)
        m.disabled = disabled != 0;
    return m;
}

ModStatus modStatus(bool windhawk, const std::optional<InstalledMod>& installed, const std::optional<std::wstring>& available) {
    if (!windhawk) return ModStatus::WindhawkMissing;
    if (!installed) return ModStatus::NotInstalled;
    if (installed->disabled) return ModStatus::Disabled;
    if (available && compareVersions(installed->version, *available) < 0) return ModStatus::UpdateAvailable;
    return ModStatus::UpToDate;
}

}  // namespace md
