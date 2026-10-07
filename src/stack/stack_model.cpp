#include "stack_model.h"

#include <windows.h>

#include <algorithm>

namespace md {

namespace {

// Ordre de l'Explorateur : casse ignorée, nombres dans l'ordre naturel (x9 avant x10).
int compareNames(const std::wstring& a, const std::wstring& b) {
    int r = CompareStringEx(LOCALE_NAME_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), int(a.size()),
                            b.c_str(), int(b.size()), nullptr, nullptr, 0);
    return r == 0 ? a.compare(b) : r - CSTR_EQUAL;
}

std::wstring extensionOf(const StackItem& it) {
    if (it.isFolder) return {};
    auto dot = it.name.rfind(L'.');
    return dot == std::wstring::npos ? std::wstring() : it.name.substr(dot + 1);
}

std::uint64_t fileTime(const FILETIME& ft) { return (std::uint64_t(ft.dwHighDateTime) << 32) | ft.dwLowDateTime; }

} // namespace

std::vector<StackItem> sortStack(std::vector<StackItem> items, StackSort sort) {
    auto byName = [](const StackItem& a, const StackItem& b) { return compareNames(a.name, b.name) < 0; };
    switch (sort) {
        case StackSort::Name: std::stable_sort(items.begin(), items.end(), byName); break;
        case StackSort::DateAdded:
        case StackSort::Modified: {
            const bool added = sort == StackSort::DateAdded;
            std::stable_sort(items.begin(), items.end(), [&](const StackItem& a, const StackItem& b) {
                std::uint64_t ta = added ? a.created : a.modified, tb = added ? b.created : b.modified;
                return ta != tb ? ta > tb : byName(a, b);   // plus récent en premier
            });
            break;
        }
        case StackSort::Kind:
            std::stable_sort(items.begin(), items.end(), [&](const StackItem& a, const StackItem& b) {
                if (a.isFolder != b.isFolder) return a.isFolder;   // dossiers d'abord
                int c = compareNames(extensionOf(a), extensionOf(b));
                return c != 0 ? c < 0 : byName(a, b);
            });
            break;
    }
    return items;
}

std::vector<StackItem> capStack(std::vector<StackItem> items, std::size_t max) {
    if (items.size() > max) items.resize(max);
    return items;
}

StackView resolveView(StackView v, std::size_t count) {
    if (v != StackView::Auto) return v;
    return count <= kFanAutoMax ? StackView::Fan : StackView::Grid;
}

std::vector<StackItem> listFolder(const std::wstring& folder) {
    std::vector<StackItem> out;
    if (folder.empty()) return out;
    std::wstring base = folder;
    if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileExW((base + L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;   // desktop.ini…
        StackItem it;
        it.path = base + name;
        it.name = std::move(name);
        it.created = fileTime(fd.ftCreationTime);
        it.modified = fileTime(fd.ftLastWriteTime);
        it.isFolder = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        out.push_back(std::move(it));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

} // namespace md
