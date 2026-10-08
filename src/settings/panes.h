// Sections de l'app Réglages et leurs lignes (logique pure) : ce qu'elles affichent, et comment elles lisent et
// changent le modèle. La fenêtre en tire ses contrôles ; les tests vérifient chaque aller-retour.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "settings_doc.h"

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

enum class RowKind { Switch, Slider, Choice, Segmented, Info };

struct PaneEnv {
    std::vector<std::wstring> screens;     // noms affichés des écrans branchés
    std::vector<std::wstring> screenIds;   // leurs noms GDI (\\.\DISPLAY1…), tels que le réglage les garde
};

struct RowSpec {
    RowKind kind = RowKind::Info;
    std::wstring label, detail, keywords;   // detail : sous-titre gris (ligne plus haute)
    double min = 0, max = 1, step = 1;      // curseur
    std::wstring minLabel, maxLabel;        // curseur : « Petite », « Grande » sous ses bouts
    std::vector<std::wstring> choices;      // menu, segmenté
    // Valeur numérique : interrupteur 0 ou 1, choix et segments par indice, curseur par sa valeur.
    std::function<double(const SettingsModel&)> get;
    std::function<void(SettingsModel&, double)> set;
    std::function<bool(const SettingsModel&)> enabled;   // vide : toujours disponible
    std::function<std::wstring(double)> format;          // curseur : valeur affichée (vide : aucune)
};

struct GroupSpec {
    std::wstring title;    // vide : groupe sans titre
    std::vector<RowSpec> rows;
    std::wstring footer;   // note grise sous le groupe
};

std::vector<GroupSpec> paneGroups(PaneId id, const PaneEnv& env);

} // namespace md
