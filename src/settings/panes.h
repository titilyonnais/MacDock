// Sections de l'app Réglages et leurs lignes (logique pure) : ce qu'elles affichent, et comment elles lisent et
// changent le modèle. La fenêtre en tire ses contrôles ; les tests vérifient chaque aller-retour.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mods.h"
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

// Info : un texte seul ; Value : un texte gris à droite ; Buttons : boutons d'action ; Shortcut : un raccourci saisi.
enum class RowKind { Switch, Slider, Choice, Segmented, Info, Buttons, Shortcut, Value };

// Actions des boutons, exécutées par la fenêtre (arg : identifiant du mod…).
enum class PaneAction { None, Launch, Restart, Quit, Export, Import, Reset, InstallMod, UninstallMod, GetWindhawk, ShowFolder, ShowLogs };
struct ButtonSpec {
    std::wstring label;
    PaneAction action = PaneAction::None;
    std::wstring arg;
};

struct ModEnv {
    ModInfo info;
    std::optional<InstalledMod> installed;
    std::optional<std::wstring> available;   // version de la source livrée
};

struct PaneEnv {
    std::vector<std::wstring> screens;     // noms affichés des écrans branchés
    std::vector<std::wstring> screenIds;   // leurs noms GDI, tels que le réglage les garde
    std::vector<std::wstring> fonts;       // familles proposées (installées)
    bool windhawk = false;                 // Windhawk installé
    std::vector<ModEnv> mods;
    bool running = true;                   // MacDock tourne
    std::wstring version, dataDir;
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
    std::vector<ButtonSpec> buttons;                      // Buttons
    // Value : texte gris à droite ; Shortcut : le réglage (« ctrl+alt+up » ou « off »).
    std::function<std::wstring(const SettingsModel&)> text;
    std::function<void(SettingsModel&, const std::wstring&)> setText;   // Shortcut
};

struct GroupSpec {
    std::wstring title;    // vide : groupe sans titre
    std::vector<RowSpec> rows;
    std::wstring footer;   // note grise sous le groupe
};

std::vector<GroupSpec> paneGroups(PaneId id, const PaneEnv& env);

// Recherche : minuscules sans accents (« Écran » → « ecran », « Œil » → « oeil »).
std::wstring searchFold(std::wstring_view s);
// Section trouvée : par son titre (`title`), ou par des lignes (indices à plat, à souligner) dont le texte (groupe,
// libellé, détail, mots-clés, choix, boutons) contient les mots que le titre n'a pas.
struct PaneMatch {
    PaneId pane;
    bool title = false;
    std::vector<int> rows;
};
// Tous les mots de `query` doivent être trouvés ; vide : toutes les sections. Dans l'ordre de la barre latérale.
std::vector<PaneMatch> searchPanes(std::wstring_view query, const PaneEnv& env);
// Barre latérale pendant une recherche : les sections trouvées (indices de paneList), et la taille de chaque groupe
// qui en garde au moins une (les groupes vides disparaissent).
struct SidebarView {
    std::vector<int> panes;
    std::vector<int> groups;
};
SidebarView sidebarView(const std::vector<PaneMatch>& matches);

// Une autre fonction (Spotlight, Mission Control, Fenêtres de l'app) utilise-t-elle le même raccourci que `name` ?
// Son nom, ou vide.
std::wstring shortcutConflict(const SettingsModel& m, const std::wstring& name);

} // namespace md
