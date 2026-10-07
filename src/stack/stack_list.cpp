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
            // Une entrée à sous-menu ne s'active pas : le sous-dossier lui-même reste ouvrable par ce lien.
            if (!e.submenu.empty()) {
                e.submenu.push_back({});
                e.submenu.push_back({addPath(paths, it.path), L"Ouvrir dans l'Explorateur"});
            }
        }
        m.items.push_back(std::move(e));
    }
    if (!m.items.empty()) m.items.push_back({});
    m.items.push_back({addPath(paths, folder), L"Ouvrir dans l'Explorateur"});
    return m;
}

void assignListIcons(MenuModel& menu, const std::vector<std::wstring>& paths, int budget,
                     const std::function<IconProvider::ImagePtr(const std::wstring&)>& load) {
    auto level = [&](std::vector<MenuItem>& entries) {
        for (auto& e : entries)
            if (e.id >= kStackListBase && std::size_t(e.id - kStackListBase) < paths.size() && budget-- > 0)
                e.icon = load(paths[std::size_t(e.id - kStackListBase)]);
    };
    level(menu.items);
    for (auto& e : menu.items) level(e.submenu);
}

} // namespace md
