#include "recent_items.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

#include "../core/strings.h"

namespace md {
namespace {

bool sameTarget(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), int(a.size()), b.c_str(), int(b.size()), TRUE) == CSTR_EQUAL;
}

bool endsWithLnk(const std::wstring& name) {
    return name.size() > 4 && sameTarget(name.substr(name.size() - 4), L".lnk");
}

// « rapport.docx » : extension de 1 à 6 lettres ou chiffres après le dernier point.
bool hasDocumentExtension(const std::wstring& name) {
    const std::size_t dot = name.rfind(L'.');
    if (dot == std::wstring::npos || dot == 0 || name.size() - dot - 1 < 1 || name.size() - dot - 1 > 6) return false;
    return std::all_of(name.begin() + std::ptrdiff_t(dot) + 1, name.end(), [](wchar_t c) { return std::iswalnum(c) != 0; });
}

} // namespace

void pushRecent(std::vector<RecentEntry>& list, RecentEntry e, std::size_t max) {
    if (e.target.empty()) return;
    std::erase_if(list, [&](const RecentEntry& x) { return sameTarget(x.target, e.target); });
    list.insert(list.begin(), std::move(e));
    if (list.size() > max) list.resize(max);
}

std::vector<RecentEntry> recentDocuments(const std::wstring& folder, std::size_t max, std::uint64_t clearedAt) {
    struct Found {
        std::uint64_t time;
        RecentEntry entry;
    };
    std::vector<Found> found;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((folder + L"\\*.lnk").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring file = fd.cFileName;
        if (!endsWithLnk(file)) continue;
        std::wstring name = file.substr(0, file.size() - 4);
        if (!hasDocumentExtension(name)) continue;
        const std::uint64_t t = (std::uint64_t(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
        if (t <= clearedAt) continue;
        found.push_back({t, {std::move(name), folder + L"\\" + file, nullptr}});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(found.begin(), found.end(), [](const Found& a, const Found& b) { return a.time > b.time; });
    std::vector<RecentEntry> out;
    for (std::size_t i = 0; i < found.size() && i < max; ++i) out.push_back(std::move(found[i].entry));
    return out;
}

RecentState recentFromJson(const json::Value& v) {
    RecentState s;
    if (!v.isObject()) return s;
    if (auto* apps = v.find("apps"); apps && apps->isArray()) {
        for (const auto& a : apps->asArray()) {
            if (!a.isObject() || s.apps.size() >= kRecentMax) continue;
            RecentEntry e;
            if (auto* n = a.find("name")) e.name = fromUtf8(n->asString(""));
            if (auto* t = a.find("target")) e.target = fromUtf8(t->asString(""));
            if (!e.name.empty() && !e.target.empty()) s.apps.push_back(std::move(e));
        }
    }
    // Chaîne : un FILETIME dépasse la précision d'un nombre JSON.
    if (auto* c = v.find("clearedAt")) s.clearedAt = std::wcstoull(fromUtf8(c->asString("0")).c_str(), nullptr, 10);
    return s;
}

json::Value recentToJson(const RecentState& s) {
    json::Value v;
    v.set("version", 1);
    json::Value apps{json::Array{}};
    for (const auto& a : s.apps) {
        json::Value e;
        e.set("name", toUtf8(a.name));
        e.set("target", toUtf8(a.target));
        apps.push(e);
    }
    v.set("apps", apps);
    v.set("clearedAt", std::to_string(s.clearedAt));
    return v;
}

} // namespace md
