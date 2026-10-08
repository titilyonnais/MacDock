#include "panes.h"

#include <algorithm>
#include <cmath>

#include "../interact/hotkey.h"

namespace md {

namespace {

// Interrupteur sur un champ booléen du modèle ; `field` est un lambda générique (modèle constant ou non).
template <class F>
RowSpec toggle(std::wstring label, F field, std::wstring detail = {}, std::wstring keywords = {}) {
    RowSpec r;
    r.kind = RowKind::Switch;
    r.label = std::move(label);
    r.detail = std::move(detail);
    r.keywords = std::move(keywords);
    r.get = [field](const SettingsModel& m) { return field(m) ? 1.0 : 0.0; };
    r.set = [field](SettingsModel& m, double v) { field(m) = v >= 0.5; };
    return r;
}

// Menu ou contrôle segmenté sur un champ énuméré : libellés et valeurs dans le même ordre.
template <class F, class E>
RowSpec pick(RowKind kind, std::wstring label, std::vector<std::wstring> labels, std::vector<E> values, F field,
             std::wstring detail = {}) {
    RowSpec r;
    r.kind = kind;
    r.label = std::move(label);
    r.detail = std::move(detail);
    r.choices = std::move(labels);
    r.get = [field, values](const SettingsModel& m) {
        const auto it = std::find(values.begin(), values.end(), field(m));
        return double(it == values.end() ? 0 : it - values.begin());
    };
    r.set = [field, values](SettingsModel& m, double v) {
        const std::size_t i = std::size_t(std::clamp(std::lround(v), 0L, long(values.size()) - 1));
        field(m) = values[i];
    };
    return r;
}

double stepped(double v, double step) { return std::round(v / step) * step; }

std::vector<GroupSpec> dockPane(const PaneEnv& env) {
    std::vector<GroupSpec> out;

    GroupSpec size;
    RowSpec tile;
    tile.kind = RowKind::Slider;
    tile.label = L"Taille";
    tile.keywords = L"icônes grandeur dock";
    tile.min = 16;
    tile.max = 128;
    tile.minLabel = L"Petite";
    tile.maxLabel = L"Grande";
    tile.get = [](const SettingsModel& m) { return m.dock.tileSize; };
    tile.set = [](SettingsModel& m, double v) {
        m.dock.tileSize = std::clamp(stepped(v, 1), 16.0, 128.0);
        m.dock.largeSize = std::max(m.dock.largeSize, m.dock.tileSize);   // jamais plus petite que les icônes
    };
    size.rows.push_back(tile);
    size.rows.push_back(toggle(L"Agrandissement", [](auto& m) -> auto& { return m.dock.magnification; }, {},
                               L"loupe grossissement survol"));
    RowSpec large = tile;
    large.label = L"Taille agrandie";
    large.keywords = L"agrandissement loupe";
    large.get = [](const SettingsModel& m) { return m.dock.largeSize; };
    large.set = [](SettingsModel& m, double v) { m.dock.largeSize = std::clamp(stepped(v, 1), m.dock.tileSize, 128.0); };
    large.enabled = [](const SettingsModel& m) { return m.dock.magnification; };
    size.rows.push_back(large);
    out.push_back(size);

    GroupSpec place;
    place.rows.push_back(pick(RowKind::Segmented, L"Position à l'écran", {L"Gauche", L"Bas", L"Droite"},
                              std::vector<DockPosition>{DockPosition::Left, DockPosition::Bottom, DockPosition::Right},
                              [](auto& m) -> auto& { return m.dock.position; }));
    RowSpec screen;
    screen.kind = RowKind::Choice;
    screen.label = L"Écran du Dock";
    screen.keywords = L"moniteur affichage";
    screen.choices.push_back(L"Écran principal");
    for (const auto& s : env.screens) screen.choices.push_back(s);
    const std::vector<std::wstring> ids = env.screenIds;
    screen.get = [ids](const SettingsModel& m) {
        const auto it = std::find(ids.begin(), ids.end(), m.dock.screen);
        return it == ids.end() ? 0.0 : double(it - ids.begin() + 1);
    };
    screen.set = [ids](SettingsModel& m, double v) {
        const long i = std::lround(v);
        m.dock.screen = i >= 1 && std::size_t(i) <= ids.size() ? ids[std::size_t(i) - 1] : std::wstring();
    };
    place.rows.push_back(screen);
    place.rows.push_back(toggle(L"Masquer et afficher le Dock automatiquement",
                                [](auto& m) -> auto& { return m.dock.autohide; }, {}, L"cacher masquage"));
    out.push_back(place);

    GroupSpec windows;
    windows.rows.push_back(pick(RowKind::Choice, L"Réduire les fenêtres avec l'effet",
                                {L"Génie", L"Échelle", L"Windows"},
                                std::vector<MinimizeEffect>{MinimizeEffect::Genie, MinimizeEffect::Scale, MinimizeEffect::Windows},
                                [](auto& m) -> auto& { return m.dock.minimizeEffect; }));
    windows.rows.push_back(toggle(L"Afficher les apps suggérées et récentes dans le Dock",
                                  [](auto& m) -> auto& { return m.dock.showRecents; }, {}, L"récents"));
    windows.rows.push_back(toggle(L"Verre Liquid Glass", [](auto& m) -> auto& { return m.dock.glass; },
                                  L"Le Dock reflète et déforme ce qui se trouve derrière lui", L"transparence flou"));
    windows.rows.push_back(toggle(L"Icônes uniformes", [](auto& m) -> auto& { return m.dock.tahoeStrictIcons; },
                                  L"Chaque icône d'app dans un carré arrondi, comme sur macOS", L"tahoe carré"));
    out.push_back(windows);
    return out;
}

std::vector<GroupSpec> menuBarPane() {
    std::vector<GroupSpec> out;
    GroupSpec bar;
    bar.rows.push_back(toggle(L"Masquer et afficher la barre des menus automatiquement",
                              [](auto& m) -> auto& { return m.bar.autohide; }, {}, L"cacher masquage"));
    out.push_back(bar);

    GroupSpec clock;
    clock.title = L"Horloge";
    clock.rows.push_back(toggle(L"Jour de la semaine", [](auto& m) -> auto& { return m.bar.clock.weekday; }, {}, L"heure"));
    clock.rows.push_back(toggle(L"Date", [](auto& m) -> auto& { return m.bar.clock.date; }, {}, L"heure"));
    clock.rows.push_back(toggle(L"Secondes", [](auto& m) -> auto& { return m.bar.clock.seconds; }, {}, L"heure"));
    clock.rows.push_back(toggle(L"Format 24 heures", [](auto& m) -> auto& { return m.bar.clock.hour24; }, {}, L"heure"));
    out.push_back(clock);

    GroupSpec icons;
    icons.title = L"Icônes";
    icons.rows.push_back(toggle(L"Son", [](auto& m) -> auto& { return m.bar.showSound; }, {}, L"volume"));
    icons.rows.push_back(toggle(L"Wi-Fi", [](auto& m) -> auto& { return m.bar.showNetwork; }, {}, L"réseau"));
    icons.rows.push_back(toggle(L"Batterie", [](auto& m) -> auto& { return m.bar.showBattery; }));
    icons.rows.push_back(toggle(L"Recherche", [](auto& m) -> auto& { return m.bar.showSearch; }, {}, L"spotlight loupe"));
    icons.rows.push_back(toggle(L"Icônes des autres apps", [](auto& m) -> auto& { return m.bar.showAppIcons; },
                                L"Relayées par le mod Windhawk de MacDock", L"zone de notification"));
    out.push_back(icons);

    GroupSpec volume;
    volume.title = L"Volume";
    volume.rows.push_back(toggle(L"Pastille du volume et de la luminosité", [](auto& m) -> auto& { return m.bar.hud; },
                                 L"Remplace l'affichage de Windows quand on règle le volume au clavier", L"hud"));
    volume.rows.push_back(toggle(L"Son quand le volume change", [](auto& m) -> auto& { return m.bar.volumeFeedback; }, {},
                                 L"pop"));
    out.push_back(volume);
    return out;
}

std::vector<GroupSpec> windowsPane() {
    std::vector<GroupSpec> out;
    GroupSpec g;
    g.rows.push_back(pick(RowKind::Choice, L"Pastilles fermer, réduire et agrandir",
                          {L"Fenêtres à barre de titre", L"Toutes les fenêtres", L"Aucune"},
                          std::vector<LightsMode>{LightsMode::Standard, LightsMode::All, LightsMode::Off},
                          [](auto& m) -> auto& { return m.bar.trafficLights; },
                          L"À la place des boutons de Windows"));
    g.rows.push_back(toggle(L"Apparence macOS des fenêtres", [](auto& m) -> auto& { return m.bar.macWindows; },
                            L"Coins arrondis, sans liseré coloré, barre de titre grise", L"coins bordure"));
    out.push_back(g);
    return out;
}

// Ligne de boutons (avec un texte gris facultatif avant eux).
RowSpec actions(std::wstring label, std::vector<ButtonSpec> buttons, std::wstring detail = {},
                std::function<std::wstring(const SettingsModel&)> text = {}) {
    RowSpec r;
    r.kind = RowKind::Buttons;
    r.label = std::move(label);
    r.detail = std::move(detail);
    r.buttons = std::move(buttons);
    r.text = std::move(text);
    return r;
}

RowSpec value(std::wstring label, std::wstring shown) {
    RowSpec r;
    r.kind = RowKind::Value;
    r.label = std::move(label);
    r.text = [shown](const SettingsModel&) { return shown; };
    return r;
}

// Raccourci saisi : le réglage tel quel (« ctrl+alt+up » ou « off »).
template <class F>
RowSpec shortcut(std::wstring label, F field, std::wstring detail = {}) {
    RowSpec r;
    r.kind = RowKind::Shortcut;
    r.label = std::move(label);
    r.detail = std::move(detail);
    r.keywords = L"raccourci clavier touche";
    r.text = [field](const SettingsModel& m) { return field(m); };
    r.setText = [field](SettingsModel& m, const std::wstring& v) { field(m) = v; };
    return r;
}

std::vector<GroupSpec> generalPane(const PaneEnv& env) {
    std::vector<GroupSpec> out;
    GroupSpec start;
    start.rows.push_back(toggle(L"Ouvrir MacDock à l'ouverture de session", [](auto& m) -> auto& { return m.startup; },
                                L"Le Dock et la barre des menus démarrent avec Windows", L"démarrage session"));
    out.push_back(start);

    GroupSpec app;
    app.title = L"MacDock";
    const bool running = env.running;
    app.rows.push_back(actions(L"MacDock",
                               running ? std::vector<ButtonSpec>{{L"Relancer", PaneAction::Restart}, {L"Quitter", PaneAction::Quit}}
                                       : std::vector<ButtonSpec>{{L"Ouvrir", PaneAction::Launch}},
                               {}, [running](const SettingsModel&) { return std::wstring(running ? L"En marche" : L"Arrêté"); }));
    out.push_back(app);

    GroupSpec backup;
    backup.title = L"Sauvegarde";
    backup.rows.push_back(actions(L"Réglages", {{L"Exporter…", PaneAction::Export}, {L"Importer…", PaneAction::Import}},
                                  L"Tous les réglages du Dock et de la barre dans un fichier"));
    backup.rows.push_back(actions(L"Valeurs par défaut", {{L"Rétablir…", PaneAction::Reset}}, L"Les apps épinglées restent dans le Dock"));
    out.push_back(backup);
    return out;
}

std::vector<GroupSpec> desktopPane() {
    std::vector<GroupSpec> out;
    GroupSpec corners;
    corners.title = L"Coins actifs";
    const std::vector<std::wstring> labels{L"—", L"Mission Control", L"Bureau", L"Apps", L"Centre de notifications",
                                           L"Verrouiller l'écran", L"Mettre l'écran en veille", L"Économiseur d'écran"};
    const std::vector<HotCornerAction> actionsList{HotCornerAction::Off, HotCornerAction::MissionControl, HotCornerAction::Desktop,
                                                   HotCornerAction::Apps, HotCornerAction::NotificationCenter, HotCornerAction::LockScreen,
                                                   HotCornerAction::DisplaySleep, HotCornerAction::ScreenSaver};
    const std::pair<const wchar_t*, Corner> names[] = {{L"En haut à gauche", Corner::TopLeft},
                                                       {L"En haut à droite", Corner::TopRight},
                                                       {L"En bas à gauche", Corner::BottomLeft},
                                                       {L"En bas à droite", Corner::BottomRight}};
    for (const auto& [name, corner] : names) {
        const std::size_t c = std::size_t(corner);
        corners.rows.push_back(pick(RowKind::Choice, name, labels, actionsList, [c](auto& m) -> auto& { return m.dock.hotCorners[c]; }));
    }
    corners.footer = L"Pousse le pointeur dans un coin de l'écran pour lancer l'action.";
    out.push_back(corners);

    GroupSpec keys;
    keys.title = L"Raccourcis";
    keys.rows.push_back(shortcut(L"Mission Control", [](auto& m) -> auto& { return m.dock.missionControlHotkey; }));
    keys.rows.push_back(shortcut(L"Fenêtres de l'app", [](auto& m) -> auto& { return m.dock.appExposeHotkey; },
                                 L"Exposé de l'app au premier plan"));
    out.push_back(keys);
    return out;
}

std::vector<GroupSpec> keyboardPane() {
    std::vector<GroupSpec> out;
    GroupSpec command;
    command.rows.push_back(toggle(L"Alt de gauche joue ⌘", [](auto& m) -> auto& { return m.dock.altAsCommand; },
                                  L"⌘C, ⌘V, ⌘Q… comme sur Mac ; Alt Gr n'est jamais touché", L"commande cmd"));
    out.push_back(command);
    GroupSpec keys;
    keys.title = L"Raccourcis";
    keys.rows.push_back(shortcut(L"Spotlight", [](auto& m) -> auto& { return m.dock.spotlightHotkey; }));
    keys.rows.push_back(pick(RowKind::Choice, L"Sélecteur d'apps", {L"⌥⇥ façon macOS", L"Celui de Windows"},
                             std::vector<std::wstring>{L"alt+tab", L"off"}, [](auto& m) -> auto& { return m.dock.appSwitcherHotkey; }));
    out.push_back(keys);
    return out;
}

std::vector<GroupSpec> screenshotsPane() {
    GroupSpec g;
    g.rows.push_back(toggle(L"Captures façon macOS", [](auto& m) -> auto& { return m.dock.screenshots; },
                            L"⊞⇧3 l'écran, ⊞⇧4 une zone ou une fenêtre, ⊞⇧5 la barre d'outils", L"capture enregistrement"));
    g.footer = L"Désactivées, ces touches reviennent à Windows. Les captures vont sur le Bureau.";
    return {g};
}

std::vector<GroupSpec> soundsPane() {
    GroupSpec g;
    g.rows.push_back(toggle(L"Sons système", [](auto& m) -> auto& { return m.dock.sounds; },
                            L"Capture d'écran, Corbeille vidée, « poof » d'une icône retirée du Dock"));
    g.rows.push_back(toggle(L"Son quand le volume change", [](auto& m) -> auto& { return m.bar.volumeFeedback; }, {}, L"pop"));
    return {g};
}

// Choix d'une police : « Automatique » (vide), puis les familles proposées.
template <class F>
RowSpec fontChoice(std::wstring label, const std::vector<std::wstring>& fonts, F field) {
    RowSpec r;
    r.kind = RowKind::Choice;
    r.label = std::move(label);
    r.keywords = L"police caractères";
    r.choices.push_back(L"Automatique");
    for (const auto& f : fonts) r.choices.push_back(f);
    r.get = [fonts, field](const SettingsModel& m) {
        const auto it = std::find(fonts.begin(), fonts.end(), field(m));
        return it == fonts.end() ? 0.0 : double(it - fonts.begin() + 1);
    };
    r.set = [fonts, field](SettingsModel& m, double v) {
        const long i = std::lround(v);
        field(m) = i >= 1 && std::size_t(i) <= fonts.size() ? fonts[std::size_t(i) - 1] : std::wstring();
    };
    return r;
}

std::vector<GroupSpec> fontPane(const PaneEnv& env) {
    GroupSpec g;
    g.rows.push_back(fontChoice(L"Police du Dock", env.fonts, [](auto& m) -> auto& { return m.dock.font; }));
    g.rows.push_back(fontChoice(L"Police de la barre des menus", env.fonts, [](auto& m) -> auto& { return m.bar.font; }));
    g.footer = L"Automatique : SF Pro si elle est installée, sinon Inter, sinon Segoe UI Variable. La police des autres apps "
               L"se règle avec le mod « Police macOS ».";
    return {g};
}

std::vector<GroupSpec> modsPane(const PaneEnv& env) {
    std::vector<GroupSpec> out;
    GroupSpec wh;
    const bool windhawk = env.windhawk;
    wh.rows.push_back(actions(L"Windhawk",
                              windhawk ? std::vector<ButtonSpec>{} : std::vector<ButtonSpec>{{L"Télécharger…", PaneAction::GetWindhawk}},
                              L"Les mods de MacDock s'installent dans Windhawk",
                              [windhawk](const SettingsModel&) { return std::wstring(windhawk ? L"Installé" : L"Non installé"); }));
    out.push_back(wh);
    for (const ModEnv& mod : env.mods) {
        GroupSpec g;
        g.title = mod.info.title;
        std::wstring shown = mod.installed ? L"Installé : " + mod.installed->version + (mod.installed->disabled ? L" (désactivé)" : L"")
                                           : std::wstring(L"Non installé");
        if (mod.available) shown += L" · disponible : " + *mod.available;
        g.rows.push_back(value(L"Version", shown));
        std::vector<ButtonSpec> buttons;
        switch (modStatus(windhawk, mod.installed, mod.available)) {
            case ModStatus::WindhawkMissing: break;
            case ModStatus::NotInstalled: buttons = {{L"Installer…", PaneAction::InstallMod, mod.info.id}}; break;
            case ModStatus::UpdateAvailable:
                buttons = {{L"Mettre à jour…", PaneAction::InstallMod, mod.info.id}, {L"Retirer…", PaneAction::UninstallMod, mod.info.id}};
                break;
            case ModStatus::UpToDate:
            case ModStatus::Disabled:
                buttons = {{L"Réinstaller…", PaneAction::InstallMod, mod.info.id}, {L"Retirer…", PaneAction::UninstallMod, mod.info.id}};
                break;
        }
        g.rows.push_back(actions(mod.info.detail, std::move(buttons)));
        g.footer = windhawk ? L"Windows demande l'autorisation administrateur." : L"Installe d'abord Windhawk.";
        out.push_back(std::move(g));
    }
    return out;
}

std::vector<GroupSpec> aboutPane(const PaneEnv& env) {
    GroupSpec g;
    g.rows.push_back(value(L"Version", env.version));
    g.rows.push_back(actions(L"Dossier des réglages", {{L"Afficher", PaneAction::ShowFolder}}, env.dataDir));
    g.rows.push_back(actions(L"Journaux", {{L"Afficher", PaneAction::ShowLogs}}));
    g.footer = L"MacDock : un Dock, une barre des menus et des réglages façon macOS pour Windows 11. Aucune ressource Apple : "
               L"tout est dessiné.";
    return {g};
}

}  // namespace

std::wstring shortcutConflict(const SettingsModel& m, const std::wstring& name) {
    const std::pair<const wchar_t*, const std::wstring*> all[] = {{L"Spotlight", &m.dock.spotlightHotkey},
                                                                  {L"Mission Control", &m.dock.missionControlHotkey},
                                                                  {L"Fenêtres de l'app", &m.dock.appExposeHotkey}};
    const std::wstring* self = nullptr;
    for (const auto& [n, v] : all)
        if (name == n) self = v;
    if (!self) return {};
    for (const auto& [n, v] : all)
        if (v != self && hotkeyConflict(*self, *v)) return n;
    return {};
}

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

std::vector<GroupSpec> paneGroups(PaneId id, const PaneEnv& env) {
    switch (id) {
        case PaneId::Dock: return dockPane(env);
        case PaneId::MenuBar: return menuBarPane();
        case PaneId::Windows: return windowsPane();
        case PaneId::General: return generalPane(env);
        case PaneId::Desktop: return desktopPane();
        case PaneId::Keyboard: return keyboardPane();
        case PaneId::Screenshots: return screenshotsPane();
        case PaneId::Sounds: return soundsPane();
        case PaneId::Font: return fontPane(env);
        case PaneId::Mods: return modsPane(env);
        case PaneId::About: return aboutPane(env);
        default: return {};
    }
}

} // namespace md
