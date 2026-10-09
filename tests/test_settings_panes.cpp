// App Réglages : les onze sections (plan 42) — lignes de valeur, raccourcis, boutons d'action, conflits.
#include <windows.h>

#include <cmath>
#include <string>

#include "minitest.h"
#include "../src/settings/panes.h"
#include "../src/settings/screens.h"

namespace {
md::PaneEnv fullEnv() {
    md::PaneEnv env;
    env.screens = {L"Écran 1", L"Écran 2"};
    env.screenIds = {L"DISPLAY1", L"DISPLAY2"};
    env.fonts = {L"SF Pro", L"Inter"};
    env.windhawk = true;
    env.running = true;
    env.version = L"2026.10.08";
    env.dataDir = L"C:/Users/x/AppData/Roaming/MacDock";
    for (const auto& mod : md::macdockMods()) env.mods.push_back({mod, md::InstalledMod{L"1.2.0", false}, L"1.2.0"});
    return env;
}

const md::RowSpec* row(const std::vector<md::GroupSpec>& groups, const wchar_t* label) {
    for (const auto& g : groups)
        for (const auto& r : g.rows)
            if (r.label == label) return &r;
    return nullptr;
}

bool hasAction(const md::RowSpec* r, md::PaneAction a) {
    if (!r) return false;
    for (const auto& b : r->buttons)
        if (b.action == a) return true;
    return false;
}
} // namespace

TEST_CASE(settings_every_pane_ready_with_rows) {
    const md::PaneEnv env = fullEnv();
    for (const auto& p : md::paneList()) {
        CHECK(p.ready);
        const auto groups = md::paneGroups(p.id, env);
        if (groups.empty()) fprintf(stderr, "section vide : %ls\n", p.title.c_str());
        CHECK(!groups.empty());
    }
}

TEST_CASE(settings_every_value_row_roundtrips) {
    const md::PaneEnv env = fullEnv();
    for (const auto& p : md::paneList())
        for (const auto& g : md::paneGroups(p.id, env))
            for (const auto& r : g.rows) {
                if (r.kind == md::RowKind::Shortcut) {
                    REQUIRE(r.text && r.setText);
                    md::SettingsModel m;
                    r.setText(m, L"ctrl+alt+k");
                    CHECK(r.text(m) == L"ctrl+alt+k");
                    r.setText(m, L"off");
                    CHECK(r.text(m) == L"off");
                    continue;
                }
                if (r.kind != md::RowKind::Switch && r.kind != md::RowKind::Slider && r.kind != md::RowKind::Choice &&
                    r.kind != md::RowKind::Segmented)
                    continue;
                REQUIRE(r.get && r.set);
                std::vector<double> values;
                if (r.kind == md::RowKind::Switch) values = {1, 0};
                else if (r.kind == md::RowKind::Slider) values = {r.max, r.min};
                else for (std::size_t i = 0; i < r.choices.size(); ++i) values.push_back(double(i));
                for (double v : values) {
                    md::SettingsModel m;
                    m.dock.tileSize = 16;
                    r.set(m, v);
                    if (std::abs(r.get(m) - v) > 1e-9) fprintf(stderr, "%ls / %ls : %g → %g\n", p.title.c_str(), r.label.c_str(), v, r.get(m));
                    CHECK_NEAR(r.get(m), v, 1e-9);
                }
            }
}

TEST_CASE(settings_general_startup_and_backup) {
    const auto general = md::paneGroups(md::PaneId::General, fullEnv());
    const md::RowSpec* startup = row(general, L"Ouvrir MacDock à l'ouverture de session");
    REQUIRE(startup && startup->kind == md::RowKind::Switch);
    md::SettingsModel m;
    startup->set(m, 1);
    CHECK(m.startup);
    CHECK(hasAction(row(general, L"Réglages"), md::PaneAction::Export));
    CHECK(hasAction(row(general, L"Réglages"), md::PaneAction::Import));
    CHECK(hasAction(row(general, L"Valeurs par défaut"), md::PaneAction::Reset));
    CHECK(hasAction(row(general, L"MacDock"), md::PaneAction::Restart));
    md::PaneEnv stopped = fullEnv();
    stopped.running = false;
    CHECK(hasAction(row(md::paneGroups(md::PaneId::General, stopped), L"MacDock"), md::PaneAction::Launch));
}

TEST_CASE(settings_hot_corners_and_shortcuts) {
    const md::PaneEnv env = fullEnv();
    const auto desktop = md::paneGroups(md::PaneId::Desktop, env);
    const md::RowSpec* corner = row(desktop, L"En bas à gauche");
    REQUIRE(corner && corner->kind == md::RowKind::Choice);
    md::SettingsModel m;
    corner->set(m, 1);
    CHECK(m.dock.hotCorners[std::size_t(md::Corner::BottomLeft)] == md::HotCornerAction::MissionControl);
    const md::RowSpec* mission = row(desktop, L"Mission Control");
    REQUIRE(mission && mission->kind == md::RowKind::Shortcut);
    CHECK(mission->text(m) == L"ctrl+alt+up");
    const auto keyboard = md::paneGroups(md::PaneId::Keyboard, env);
    const md::RowSpec* spotlight = row(keyboard, L"Spotlight");
    REQUIRE(spotlight && spotlight->kind == md::RowKind::Shortcut);
    // Deux fonctions sur la même combinaison : le conflit est nommé.
    mission->setText(m, L"Alt + Space");
    CHECK(md::shortcutConflict(m, L"Spotlight") == L"Mission Control");
    CHECK(md::shortcutConflict(m, L"Mission Control") == L"Spotlight");
    mission->setText(m, L"ctrl+alt+up");
    CHECK(md::shortcutConflict(m, L"Spotlight").empty());
    const md::RowSpec* switcher = row(keyboard, L"Sélecteur d'apps");
    REQUIRE(switcher);
    switcher->set(m, 1);
    CHECK(m.dock.appSwitcherHotkey == L"off");
}

TEST_CASE(settings_fonts_and_mods) {
    md::PaneEnv env = fullEnv();
    const auto fontGroups = md::paneGroups(md::PaneId::Font, env);   // gardés : la ligne pointe dedans
    const md::RowSpec* dockFont = row(fontGroups, L"Police du Dock");
    REQUIRE(dockFont && dockFont->choices.size() == 3);   // Automatique, SF Pro, Inter
    md::SettingsModel m;
    dockFont->set(m, 2);
    CHECK(m.dock.font == L"Inter");
    dockFont->set(m, 0);
    CHECK(m.dock.font.empty());
    // Mods : à jour → Réinstaller et Retirer ; pas installé → Installer ; sans Windhawk → aucun bouton.
    const std::wstring title = md::macdockMods().front().title;
    auto buttons = [&](const md::PaneEnv& e) {
        for (const auto& g : md::paneGroups(md::PaneId::Mods, e))
            if (g.title == title)
                for (const auto& r : g.rows)
                    if (r.kind == md::RowKind::Buttons) return &r - &g.rows[0] >= 0 ? r.buttons : std::vector<md::ButtonSpec>{};
        return std::vector<md::ButtonSpec>{};
    };
    auto has = [](const std::vector<md::ButtonSpec>& bs, md::PaneAction a) {
        for (const auto& b : bs)
            if (b.action == a) return true;
        return false;
    };
    CHECK(has(buttons(env), md::PaneAction::InstallMod) && has(buttons(env), md::PaneAction::UninstallMod));
    env.mods[0].installed.reset();
    CHECK(has(buttons(env), md::PaneAction::InstallMod) && !has(buttons(env), md::PaneAction::UninstallMod));
    env.windhawk = false;
    CHECK(!has(buttons(env), md::PaneAction::InstallMod));
}

TEST_CASE(settings_search_folds_case_and_accents) {
    CHECK(md::searchFold(L"Général À propos Œil ÉCRAN") == L"general a propos oeil ecran");
    CHECK(md::searchPanes(L"", fullEnv()).size() == md::paneList().size());   // vide : toutes les sections
    CHECK(md::searchPanes(L"zzzz", fullEnv()).empty());
    auto find = [](const std::vector<md::PaneMatch>& all, md::PaneId id) -> const md::PaneMatch* {
        for (const auto& m : all)
            if (m.pane == id) return &m;
        return nullptr;
    };
    const auto ecran = md::searchPanes(L"ecran", fullEnv());   // « Captures d'écran », « Écran du Dock »
    REQUIRE(find(ecran, md::PaneId::Screenshots) != nullptr);
    CHECK(find(ecran, md::PaneId::Screenshots)->title);
    REQUIRE(find(ecran, md::PaneId::Dock) != nullptr);
    CHECK(!find(ecran, md::PaneId::Dock)->title);
    CHECK(!find(ecran, md::PaneId::Dock)->rows.empty());   // la ligne trouvée, à souligner
    const auto keys = md::searchPanes(L"RACCOURCI", fullEnv());   // mots-clés des champs de raccourci
    CHECK(find(keys, md::PaneId::Keyboard) != nullptr);
    CHECK(find(keys, md::PaneId::Desktop) != nullptr);
    const auto corners = md::searchPanes(L"coins actifs", fullEnv());   // tous les mots, titre de groupe compris
    REQUIRE(find(corners, md::PaneId::Desktop) != nullptr);
    CHECK(find(corners, md::PaneId::Desktop)->rows.size() == 4);
    CHECK(md::searchPanes(L"loupe", fullEnv()).front().pane == md::PaneId::Dock);   // mot-clé de l'agrandissement
}

TEST_CASE(settings_sidebar_keeps_only_found_panes) {
    // Sans recherche : toutes les sections, dans leurs quatre groupes.
    const md::SidebarView all = md::sidebarView(md::searchPanes(L"", fullEnv()));
    CHECK(all.panes.size() == md::paneList().size());
    CHECK(all.groups == md::sidebarGroups());
    // Recherche : seulement les sections trouvées, les groupes vides disparaissent.
    std::vector<md::PaneMatch> found{{md::PaneId::Dock}, {md::PaneId::Keyboard}, {md::PaneId::Mods}};
    const md::SidebarView some = md::sidebarView(found);
    REQUIRE(some.panes.size() == 3);
    CHECK(md::paneList()[std::size_t(some.panes[0])].id == md::PaneId::Dock);
    CHECK(md::paneList()[std::size_t(some.panes[2])].id == md::PaneId::Mods);
    CHECK((some.groups == std::vector<int>{1, 1, 1}));
    CHECK(md::sidebarView({}).panes.empty());
}

TEST_CASE(settings_search_finds_button_labels) {
    // Relecture du plan 42 : « exporter », « relancer » ou « installer » ne trouvaient rien.
    const auto found = md::searchPanes(L"exporter", fullEnv());
    REQUIRE(!found.empty());
    CHECK(found.front().pane == md::PaneId::General);
    CHECK(!found.front().rows.empty());
    CHECK(!md::searchPanes(L"relancer", fullEnv()).empty());
}

TEST_CASE(settings_screen_labels) {
    // Plan 46 : les écrans portent leur nom, comme sur macOS ; sans nom, « Écran N » comme avant.
    const auto l = md::screenLabels({{L"DELL U2720Q", 3840, 2160, true}, {L"", 1920, 1080, false},
                                     {L"Écran intégré", 2560, 1600, false}});
    REQUIRE(l.size() == 3u);
    CHECK(l[0] == L"DELL U2720Q — 3840 × 2160 (principal)");
    CHECK(l[1] == L"Écran 2 — 1920 × 1080");
    CHECK(l[2] == L"Écran intégré — 2560 × 1600");
    // Deux écrans du même modèle : numérotés, comme dans les réglages Écrans de macOS.
    const auto t = md::screenLabels({{L"LG ULTRAGEAR", 1920, 1080, false}, {L"LG ULTRAGEAR", 1920, 1080, true}});
    CHECK(t[0] == L"LG ULTRAGEAR (1) — 1920 × 1080");
    CHECK(t[1] == L"LG ULTRAGEAR (2) — 1920 × 1080 (principal)");
}

TEST_CASE(settings_monitor_names_on_this_pc) {
    // Lecture seule de la configuration d'affichage : chaque nom va avec une source GDI (DISPLAY1…).
    for (const auto& [device, name] : md::monitorNames()) {
        CHECK(device.rfind(L"\\\\.\\DISPLAY", 0) == 0);
        CHECK(!name.empty());
    }
}
