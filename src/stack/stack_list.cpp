#include "stack_list.h"

#include <algorithm>

namespace md {

namespace {

int addPath(std::vector<std::wstring>& paths, const std::wstring& path) {
    paths.push_back(path);
    return kStackListBase + int(paths.size() - 1);
}

} // namespace

MenuModel stackListMenu(const std::wstring& folder, const std::vector<StackItem>& items,
                        const std::function<std::vector<StackItem>(const std::wstring&)>& listSub,
                        std::vector<std::wstring>& paths, std::size_t maxItems) {
    MenuModel m;
    const std::size_t cap = std::min(maxItems, kGridMaxItems);
    const std::size_t n = std::min(items.size(), cap);
    for (std::size_t i = 0; i < n; ++i) {
        const StackItem& it = items[i];
        MenuItem e{addPath(paths, it.path), it.name};
        if (it.isFolder && listSub) {
            // Sous-dossier : son contenu en sous-menu (un seul niveau ; plus bas, les dossiers s'ouvrent).
            auto sub = listSub(it.path);
            for (std::size_t k = 0; k < sub.size() && k < cap; ++k)
                e.submenu.push_back({addPath(paths, sub[k].path), sub[k].name});
        }
        m.items.push_back(std::move(e));
    }
    if (!m.items.empty()) m.items.push_back({});
    m.items.push_back({addPath(paths, folder), L"Ouvrir dans l'Explorateur"});
    return m;
}

} // namespace md
