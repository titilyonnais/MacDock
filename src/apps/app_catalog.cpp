#include "app_catalog.h"

#include <windows.h>

#include <algorithm>
#include <set>

namespace md {

namespace {

bool endsWith(const std::wstring& s, std::wstring_view suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::wstring trimmed(const std::wstring& s) {
    const auto b = s.find_first_not_of(L" \t");
    if (b == std::wstring::npos) return {};
    return s.substr(b, s.find_last_not_of(L" \t") - b + 1);
}

bool wordStart(const std::wstring& s, std::size_t pos) {
    return pos == 0 || std::wstring_view(L" -(._'’").find(s[pos - 1]) != std::wstring_view::npos;
}

} // namespace

std::wstring launchTarget(const AppEntry& e) { return L"shell:AppsFolder\\" + e.parsingName; }

std::wstring foldForSearch(std::wstring_view text) {
    if (text.empty()) return {};
    const int n = FoldStringW(MAP_COMPOSITE, text.data(), int(text.size()), nullptr, 0);
    std::wstring decomposed(std::size_t(std::max(n, 0)), L'\0');
    if (n <= 0 || FoldStringW(MAP_COMPOSITE, text.data(), int(text.size()), decomposed.data(), n) != n)
        decomposed.assign(text);
    std::wstring bare;
    for (wchar_t c : decomposed)
        if (c < 0x0300 || c > 0x036F) bare.push_back(c);   // marques combinantes (accents) retirées
    if (bare.empty()) return bare;
    std::wstring lower(bare.size(), L'\0');
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, bare.c_str(), int(bare.size()), lower.data(),
                       int(lower.size()), nullptr, nullptr, 0))
        return bare;
    return lower;
}

bool isListedApp(const AppEntry& e) {
    if (trimmed(e.name).empty() || e.parsingName.empty()) return false;
    const std::wstring target = foldForSearch(e.parsingName);
    if (target.starts_with(L"http://") || target.starts_with(L"https://")) return false;   // lien web (steam:// reste)
    for (std::wstring_view ext : {L".chm", L".txt", L".pdf", L".htm", L".html", L".url", L".rtf", L".ini", L".log"})
        if (endsWith(target, ext)) return false;
    const std::wstring name = foldForSearch(trimmed(e.name));
    for (std::wstring_view prefix : {L"uninstall", L"desinstaller", L"desinstallation"})
        if (name.starts_with(prefix)) return false;
    return true;
}

std::vector<AppEntry> catalogFrom(std::vector<AppEntry> raw) {
    std::vector<AppEntry> out;
    std::set<std::wstring> seen;
    for (AppEntry& e : raw)
        if (isListedApp(e) && seen.insert(foldForSearch(e.parsingName)).second) out.push_back(std::move(e));
    const DWORD flags = LINGUISTIC_IGNORECASE | NORM_IGNORENONSPACE | SORT_DIGITSASNUMBERS;
    std::stable_sort(out.begin(), out.end(), [flags](const AppEntry& a, const AppEntry& b) {
        const int c = CompareStringEx(LOCALE_NAME_USER_DEFAULT, flags, a.name.c_str(), int(a.name.size()), b.name.c_str(),
                                      int(b.name.size()), nullptr, nullptr, 0);
        if (c != CSTR_EQUAL && c != 0) return c == CSTR_LESS_THAN;
        return a.parsingName < b.parsingName;
    });
    return out;
}

std::vector<std::size_t> searchApps(const std::vector<AppEntry>& apps, const std::wstring& query) {
    const std::wstring q = foldForSearch(trimmed(query));
    std::vector<std::size_t> groups[3];
    for (std::size_t i = 0; i < apps.size(); ++i) {
        if (q.empty()) {
            groups[0].push_back(i);
            continue;
        }
        const std::wstring n = foldForSearch(apps[i].name);
        std::size_t pos = n.find(q);
        if (pos == std::wstring::npos) continue;
        int group = pos == 0 ? 0 : 2;
        for (; group == 2 && pos != std::wstring::npos; pos = n.find(q, pos + 1))
            if (wordStart(n, pos)) group = 1;
        groups[group].push_back(i);
    }
    std::vector<std::size_t> out = std::move(groups[0]);
    out.insert(out.end(), groups[1].begin(), groups[1].end());
    out.insert(out.end(), groups[2].begin(), groups[2].end());
    return out;
}

} // namespace md
