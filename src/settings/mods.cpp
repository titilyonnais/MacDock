#include "mods.h"

#include <windows.h>

#include <algorithm>
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

namespace {
// Champs numériques (« 1.10.0 »), puis le reste : vrai si un suffixe autre que des espaces suit (préversion, « -beta »).
bool versionFields(std::wstring_view s, std::vector<int>& fields) {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == L' ' || s[i] == L'\t')) ++i;
    while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') {
        int n = 0;
        while (i < s.size() && s[i] >= L'0' && s[i] <= L'9') n = std::min(n * 10 + (s[i++] - L'0'), 100000000);
        fields.push_back(n);
        if (i + 1 < s.size() && s[i] == L'.' && s[i + 1] >= L'0' && s[i + 1] <= L'9') ++i;
        else break;
    }
    for (; i < s.size(); ++i)
        if (s[i] != L' ' && s[i] != L'\t') return true;
    return false;
}
}  // namespace

int compareVersions(std::wstring_view a, std::wstring_view b) {
    std::vector<int> x, y;
    const bool preA = versionFields(a, x), preB = versionFields(b, y);
    for (std::size_t i = 0; i < std::max(x.size(), y.size()); ++i) {   // champs manquants = 0
        const int u = i < x.size() ? x[i] : 0, v = i < y.size() ? y[i] : 0;
        if (u != v) return u < v ? -1 : 1;
    }
    if (preA != preB) return preA ? -1 : 1;   // une préversion passe avant la version elle-même
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
    // Une version plus récente passe devant l'état désactivé : « Mettre à jour… » plutôt que « Réinstaller… ».
    if (available && compareVersions(installed->version, *available) < 0) return ModStatus::UpdateAvailable;
    if (installed->disabled) return ModStatus::Disabled;
    return ModStatus::UpToDate;
}

}  // namespace md
