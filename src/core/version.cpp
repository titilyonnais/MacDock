#include "version.h"

#include <climits>
#include <vector>

namespace md {

namespace {

bool isDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }
bool isIdentChar(wchar_t c) { return isDigit(c) || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'-'; }

std::vector<std::wstring_view> split(std::wstring_view s, wchar_t sep) {
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t at = s.find(sep, start);
        parts.push_back(s.substr(start, at == std::wstring_view::npos ? std::wstring_view::npos : at - start));
        if (at == std::wstring_view::npos) return parts;
        start = at + 1;
    }
}

// Nombre décimal non vide, sans zéro en tête (semver), qui tient dans un int.
std::optional<int> number(std::wstring_view s) {
    if (s.empty() || (s.size() > 1 && s[0] == L'0')) return std::nullopt;
    long long n = 0;
    for (wchar_t c : s) {
        if (!isDigit(c)) return std::nullopt;
        n = n * 10 + (c - L'0');
        if (n > INT_MAX) return std::nullopt;
    }
    return int(n);
}

bool numeric(std::wstring_view s) {
    if (s.empty()) return false;
    for (wchar_t c : s)
        if (!isDigit(c)) return false;
    return true;
}

} // namespace

std::optional<Version> parseVersion(std::wstring_view text) {
    if (!text.empty() && (text.front() == L'v' || text.front() == L'V')) text.remove_prefix(1);
    if (const std::size_t plus = text.find(L'+'); plus != std::wstring_view::npos) {   // métadonnées : ignorées
        const std::wstring_view meta = text.substr(plus + 1);
        if (meta.empty()) return std::nullopt;
        for (wchar_t c : meta)
            if (!isIdentChar(c) && c != L'.') return std::nullopt;
        text = text.substr(0, plus);
    }
    Version v;
    if (const std::size_t dash = text.find(L'-'); dash != std::wstring_view::npos) {
        const std::wstring_view pre = text.substr(dash + 1);
        for (std::wstring_view id : split(pre, L'.')) {
            if (id.empty() || (numeric(id) && id.size() > 1 && id[0] == L'0')) return std::nullopt;   // « rc.01 »
            for (wchar_t c : id)
                if (!isIdentChar(c)) return std::nullopt;
        }
        v.pre = std::wstring(pre);
        text = text.substr(0, dash);
    }
    const auto core = split(text, L'.');
    if (core.size() < 2 || core.size() > 3) return std::nullopt;
    const auto major = number(core[0]), minor = number(core[1]);
    const auto patch = core.size() == 3 ? number(core[2]) : std::optional<int>(0);
    if (!major || !minor || !patch) return std::nullopt;
    v.major = *major;
    v.minor = *minor;
    v.patch = *patch;
    return v;
}

int compareVersions(const Version& a, const Version& b) {
    for (const auto [x, y] : {std::pair{a.major, b.major}, std::pair{a.minor, b.minor}, std::pair{a.patch, b.patch}})
        if (x != y) return x < y ? -1 : 1;
    if (a.pre.empty() != b.pre.empty()) return a.pre.empty() ? 1 : -1;   // la version publiée passe après
    const auto pa = split(a.pre, L'.'), pb = split(b.pre, L'.');
    for (std::size_t i = 0; i < pa.size() && i < pb.size(); ++i) {
        const bool na = numeric(pa[i]), nb = numeric(pb[i]);
        if (na && nb) {   // sans zéro en tête : le plus long est le plus grand, sinon ordre des chiffres (toute taille)
            if (pa[i].size() != pb[i].size()) return pa[i].size() < pb[i].size() ? -1 : 1;
            if (const int c = pa[i].compare(pb[i]); c != 0) return c < 0 ? -1 : 1;
        } else if (na != nb) {
            return na ? -1 : 1;   // un nombre avant du texte
        } else if (const int c = pa[i].compare(pb[i]); c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    if (pa.size() != pb.size()) return pa.size() < pb.size() ? -1 : 1;
    return 0;
}

std::wstring versionText(const Version& v) {
    std::wstring s = std::to_wstring(v.major) + L"." + std::to_wstring(v.minor) + L"." + std::to_wstring(v.patch);
    if (!v.pre.empty()) s += L"-" + v.pre;
    return s;
}

} // namespace md
