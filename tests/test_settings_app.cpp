// App Réglages : modèle (fichiers settings.json et menubar.json), sections, mise en page, rendu des contrôles.
#include <windows.h>

#include <string>

#include "minitest.h"
#include "../src/config/config_store.h"
#include "../src/settings/settings_doc.h"

namespace {
// Dossier temporaire propre à un test (effacé au début).
std::wstring tempDir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"macdock-settings-" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    DeleteFileW((dir + L"\\settings.json").c_str());
    DeleteFileW((dir + L"\\menubar.json").c_str());
    return dir;
}

md::json::Value readFile(const std::wstring& path) { return md::loadJsonFile(path).value; }
} // namespace

TEST_CASE(settings_merge_writes_only_changed_keys) {
    // Le fichier garde ses clés inconnues et ce que le Dock y a écrit (épingles) : seule la clé changée est réécrite.
    md::json::Value file = *md::json::parse(
        R"({"tileSize":48,"futureKey":true,"pinned":[{"kind":"app","appId":"x","launch":"C:\\x.exe","name":"X"}]})");
    md::Settings s = md::settingsFromJson(file);
    const md::json::Value before = md::settingsToJson(s);
    s.tileSize = 60;
    const md::json::Value merged = md::mergeChanged(file, before, md::settingsToJson(s));
    CHECK_NEAR(merged.find("tileSize")->asNumber(0), 60.0, 1e-9);
    CHECK(merged.find("futureKey") && merged.find("futureKey")->asBool(false));
    CHECK(md::json::serialize(*merged.find("pinned"), false) == md::json::serialize(*file.find("pinned"), false));
    CHECK(!merged.find("magnification"));   // inchangée : pas ajoutée au fichier
}

TEST_CASE(settings_commit_keeps_pins_written_by_the_dock) {
    const std::wstring dir = tempDir(L"commit");
    // Le Dock a épinglé une app : l'app Réglages, qui change la taille, ne doit pas l'effacer.
    md::saveJsonFileAtomic(dir + L"\\settings.json", *md::json::parse(
        R"({"version":2,"tileSize":48,"pinned":[{"kind":"app","appId":"a","launch":"C:\\a.exe","name":"A"}],"pinnedInitialized":true})"));
    md::SettingsModel after;
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.dock.tileSize = 64; }, &after));
    const md::json::Value f = readFile(dir + L"\\settings.json");
    CHECK_NEAR(f.find("tileSize")->asNumber(0), 64.0, 1e-9);
    REQUIRE(f.find("pinned") && f.find("pinned")->isArray());
    CHECK_EQ(f.find("pinned")->asArray().size(), std::size_t(1));
    CHECK_NEAR(after.dock.tileSize, 64.0, 1e-9);
    // La barre n'a pas changé : son fichier n'est pas créé.
    CHECK(GetFileAttributesW((dir + L"\\menubar.json").c_str()) == INVALID_FILE_ATTRIBUTES);
}

TEST_CASE(settings_commit_creates_a_missing_file) {
    const std::wstring dir = tempDir(L"missing");
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.bar.autohide = true; }));
    const md::json::Value f = readFile(dir + L"\\menubar.json");
    CHECK(f.find("autohide") && f.find("autohide")->asBool(false));
    const md::SettingsModel m = md::loadModel(dir);
    CHECK(m.bar.autohide);
    CHECK(!m.dock.autohide);   // défauts pour le Dock, sans fichier
}

#include "../src/settings/panes.h"

namespace {
const md::RowSpec* rowNamed(const std::vector<md::GroupSpec>& groups, const wchar_t* label) {
    for (const auto& g : groups)
        for (const auto& r : g.rows)
            if (r.label == label) return &r;
    return nullptr;
}
} // namespace

TEST_CASE(settings_panes_list_and_keys) {
    const auto& panes = md::paneList();
    REQUIRE(panes.size() == 11);
    CHECK(panes.front().id == md::PaneId::General);
    CHECK(md::paneFromKey("dock") == md::PaneId::Dock);
    CHECK(md::paneFromKey("menubar") == md::PaneId::MenuBar);
    CHECK(md::paneFromKey("windows") == md::PaneId::Windows);
    CHECK(!md::paneFromKey("nope").has_value());
    CHECK(md::paneInfo(md::PaneId::Dock).ready);
    CHECK(!md::paneInfo(md::PaneId::Mods).ready);   // plan 42
}

TEST_CASE(settings_panes_rows_roundtrip) {
    // Chaque ligne lit ce qu'elle écrit, pour chaque valeur possible (interrupteurs, choix, segments, curseurs).
    md::PaneEnv env;
    env.screens = {L"Écran 1", L"Écran 2"};
    env.screenIds = {L"DISPLAY1", L"DISPLAY2"};
    for (md::PaneId id : {md::PaneId::Dock, md::PaneId::MenuBar, md::PaneId::Windows}) {
        const auto groups = md::paneGroups(id, env);
        CHECK(!groups.empty());
        for (const auto& g : groups)
            for (const auto& r : g.rows) {
                if (r.kind == md::RowKind::Info) continue;
                REQUIRE(r.get && r.set);
                std::vector<double> values;
                if (r.kind == md::RowKind::Switch) values = {1, 0};
                else if (r.kind == md::RowKind::Slider) values = {r.max, r.min, (r.min + r.max) / 2};
                else for (std::size_t i = 0; i < r.choices.size(); ++i) values.push_back(double(i));
                for (double v : values) {
                    md::SettingsModel m;
                    if (r.kind == md::RowKind::Slider) m.dock.tileSize = 16;   // la taille agrandie peut descendre
                    r.set(m, v);
                    CHECK_NEAR(r.get(m), std::round(v / r.step) * r.step, 1e-9);
                }
            }
    }
}

TEST_CASE(settings_panes_dock_specifics) {
    md::PaneEnv env;
    env.screens = {L"Écran 1", L"Écran 2"};
    env.screenIds = {L"DISPLAY1", L"DISPLAY2"};
    const auto dock = md::paneGroups(md::PaneId::Dock, env);
    const md::RowSpec* large = rowNamed(dock, L"Taille agrandie");
    REQUIRE(large && large->enabled);
    md::SettingsModel m;
    m.dock.magnification = false;
    CHECK(!large->enabled(m));   // grisée sans agrandissement, comme sur Mac
    m.dock.magnification = true;
    CHECK(large->enabled(m));
    // Agrandir la taille au-delà de la taille agrandie relève celle-ci (jamais plus petite que les icônes).
    const md::RowSpec* size = rowNamed(dock, L"Taille");
    REQUIRE(size);
    m.dock.largeSize = 80;
    size->set(m, 100);
    CHECK(m.dock.largeSize >= 100);
    // Écran du Dock : « Écran principal » puis les écrans branchés.
    const md::RowSpec* screen = rowNamed(dock, L"Écran du Dock");
    REQUIRE(screen && screen->choices.size() == 3);
    screen->set(m, 2);
    CHECK(m.dock.screen == L"DISPLAY2");
    screen->set(m, 0);
    CHECK(m.dock.screen.empty());
    const md::RowSpec* position = rowNamed(dock, L"Position à l'écran");
    REQUIRE(position && position->kind == md::RowKind::Segmented);
    position->set(m, 0);
    CHECK(m.dock.position == md::DockPosition::Left);
}
