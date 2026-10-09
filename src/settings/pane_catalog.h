// Sections de l'app Réglages MacDock (logique pure, sans dépendance) : identifiant, titre, clé de --pane, tuile.
// Le Dock s'en sert aussi (Spotlight les propose, avec leur tuile).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace md {

enum class PaneId { General, Dock, MenuBar, Windows, Desktop, Keyboard, Screenshots, Sounds, Font, Mods, About };
enum class PaneIcon { Gear, Dock, MenuBar, Windows, Desktop, Keyboard, Screenshot, Sound, Font, Puzzle, Info };

struct PaneInfo {
    PaneId id;
    std::wstring title;
    std::string key;         // --pane <key>
    std::uint32_t tile;      // couleur de la tuile, 0xRRGGBB
    PaneIcon icon;
    bool ready;              // false : « Bientôt » (sections du plan 42)
};
const std::vector<PaneInfo>& paneList();   // ordre de la barre latérale
// Groupes de la barre latérale (nombre de sections de chacun, dans l'ordre de paneList) : Général | Dock, Barre des
// menus, Fenêtres, Mission Control | Clavier, Captures, Sons, Police | Mods, À propos.
const std::vector<int>& sidebarGroups();
const PaneInfo& paneInfo(PaneId id);
std::optional<PaneId> paneFromKey(std::string_view key);

} // namespace md
