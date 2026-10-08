#include "panes.h"

#include <algorithm>
#include <cmath>

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
                          L"À la place des boutons de Windows, en haut à droite"));
    g.rows.push_back(toggle(L"Apparence macOS des fenêtres", [](auto& m) -> auto& { return m.bar.macWindows; },
                            L"Coins arrondis, sans liseré coloré, barre de titre grise", L"coins bordure"));
    out.push_back(g);
    return out;
}

} // namespace

const std::vector<PaneInfo>& paneList() {
    static const std::vector<PaneInfo> panes = {
        {PaneId::General, L"Général", "general", 0x8E8E93, PaneIcon::Gear, false},
        {PaneId::Dock, L"Dock", "dock", 0x1C1C1E, PaneIcon::Dock, true},
        {PaneId::MenuBar, L"Barre des menus", "menubar", 0x0088FF, PaneIcon::MenuBar, true},
        {PaneId::Windows, L"Fenêtres", "windows", 0x6155F5, PaneIcon::Windows, true},
        {PaneId::Desktop, L"Bureau et Mission Control", "desktop", 0x00C3D0, PaneIcon::Desktop, false},
        {PaneId::Keyboard, L"Clavier", "keyboard", 0x8E8E93, PaneIcon::Keyboard, false},
        {PaneId::Screenshots, L"Captures d'écran", "screenshots", 0xFF8D28, PaneIcon::Screenshot, false},
        {PaneId::Sounds, L"Sons", "sounds", 0xFF383C, PaneIcon::Sound, false},
        {PaneId::Font, L"Police", "font", 0xAC7F5E, PaneIcon::Font, false},
        {PaneId::Mods, L"Mods Windhawk", "mods", 0x34C759, PaneIcon::Puzzle, false},
        {PaneId::About, L"À propos", "about", 0x8E8E93, PaneIcon::Info, false},
    };
    return panes;
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
        default: return {};
    }
}

} // namespace md
