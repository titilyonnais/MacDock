#include "pane_catalog.h"

namespace md {

const std::vector<PaneInfo>& paneList() {
    static const std::vector<PaneInfo> panes = {
        {PaneId::General, L"Général", "general", 0x8E8E93, PaneIcon::Gear, true},
        {PaneId::Dock, L"Dock", "dock", 0x1C1C1E, PaneIcon::Dock, true},
        {PaneId::MenuBar, L"Barre des menus", "menubar", 0x0088FF, PaneIcon::MenuBar, true},
        {PaneId::Windows, L"Fenêtres", "windows", 0x6155F5, PaneIcon::Windows, true},
        {PaneId::Desktop, L"Mission Control", "desktop", 0x00C3D0, PaneIcon::Desktop, true},
        {PaneId::Keyboard, L"Clavier", "keyboard", 0x8E8E93, PaneIcon::Keyboard, true},
        {PaneId::Screenshots, L"Captures d'écran", "screenshots", 0xFF8D28, PaneIcon::Screenshot, true},
        {PaneId::Sounds, L"Sons", "sounds", 0xFF383C, PaneIcon::Sound, true},
        {PaneId::Font, L"Police", "font", 0xAC7F5E, PaneIcon::Font, true},
        {PaneId::Mods, L"Mods Windhawk", "mods", 0x34C759, PaneIcon::Puzzle, true},
        {PaneId::About, L"À propos", "about", 0x8E8E93, PaneIcon::Info, true},
    };
    return panes;
}

const std::vector<int>& sidebarGroups() {
    static const std::vector<int> groups{1, 4, 4, 2};
    return groups;
}

const PaneInfo& paneInfo(PaneId id) {
    for (const auto& p : paneList())
        if (p.id == id) return p;
    return paneList().front();
}

std::optional<PaneId> paneFromKey(std::string_view key) {
    for (const auto& p : paneList())
        if (p.key == key) return p.id;
    return std::nullopt;
}

} // namespace md
