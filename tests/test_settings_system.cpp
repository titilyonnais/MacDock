// App Réglages : démarrage avec Windows, sauvegarde (exporter, importer, défauts) et état des mods Windhawk.
#include <windows.h>

#include <optional>
#include <string>

#include "minitest.h"
#include "../src/config/config_store.h"
#include "../src/settings/backup.h"
#include "../src/settings/mods.h"
#include "../src/settings/settings_doc.h"

namespace {
std::wstring freshDir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"macdock-system-" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    for (const wchar_t* f : {L"settings.json", L"menubar.json"}) DeleteFileW((dir + L"/" + f).c_str());
    return dir;
}

// Registre simulé : la valeur « MacDock » de la clé Run.
struct FakeRun {
    std::optional<std::wstring> value;
    md::SettingsIo io(const std::wstring& launcher) {
        md::SettingsIo io;
        io.launcherPath = launcher;
        io.readStartup = [this] { return value; };
        io.writeStartup = [this](const std::optional<std::wstring>& v) {
            value = v;
            return true;
        };
        return io;
    }
};
} // namespace

TEST_CASE(settings_startup_reads_and_writes_the_run_value) {
    const std::wstring dir = freshDir(L"startup");
    FakeRun run;
    const md::SettingsIo io = run.io(L"C:/MacDock/MacDockLauncher.exe");
    CHECK(!md::loadModel(dir, nullptr, &io).startup);
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.startup = true; }, nullptr, &io));
    REQUIRE(run.value.has_value());
    CHECK(*run.value == L"\"C:/MacDock/MacDockLauncher.exe\"");   // la même valeur que MacDockLauncher.exe --install
    CHECK(md::loadModel(dir, nullptr, &io).startup);
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.startup = false; }, nullptr, &io));
    CHECK(!run.value.has_value());
    // Un autre réglage ne touche pas au démarrage.
    run.value = L"\"ailleurs.exe\"";
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.dock.autohide = true; }, nullptr, &io));
    CHECK(run.value == std::optional<std::wstring>(L"\"ailleurs.exe\""));
}

TEST_CASE(settings_backup_export_import_roundtrip) {
    const std::wstring dir = freshDir(L"export"), other = freshDir(L"import");
    md::saveJsonFileAtomic(dir + L"/settings.json", *md::json::parse(
        R"({"version":2,"tileSize":64,"pinned":[{"kind":"app","appId":"a","launch":"C:/a.exe","name":"A"}],"pinnedInitialized":true})"));
    md::saveJsonFileAtomic(dir + L"/menubar.json", *md::json::parse(R"({"autohide":true})"));
    const md::json::Value backup = md::exportSettings(dir);
    CHECK(md::isSettingsBackup(backup));
    CHECK(md::importSettings(other, backup) == md::ImportResult::Ok);
    const md::SettingsModel m = md::loadModel(other);
    CHECK_NEAR(m.dock.tileSize, 64.0, 1e-9);
    CHECK(m.bar.autohide);
    CHECK_EQ(m.dock.pinned.size(), std::size_t(1));
    // Autre document : refusé, rien n'est écrit.
    CHECK(md::importSettings(freshDir(L"refused"), *md::json::parse(R"({"hello":1})")) == md::ImportResult::NotABackup);
    CHECK(md::importSettings(other, *md::json::parse(R"({"macdockBackup":1,"settings":[1,2]})")) == md::ImportResult::Invalid);
}

TEST_CASE(settings_reset_keeps_pinned_apps) {
    const std::wstring dir = freshDir(L"reset");
    md::saveJsonFileAtomic(dir + L"/settings.json", *md::json::parse(
        R"({"version":2,"tileSize":90,"autohide":true,"pinned":[{"kind":"app","appId":"a","launch":"C:/a.exe","name":"A"}],"pinnedInitialized":true})"));
    md::saveJsonFileAtomic(dir + L"/menubar.json", *md::json::parse(R"({"autohide":true,"showSound":false})"));
    REQUIRE(md::resetSettings(dir));
    const md::SettingsModel m = md::loadModel(dir);
    CHECK_NEAR(m.dock.tileSize, md::Settings{}.tileSize, 1e-9);
    CHECK(!m.dock.autohide);
    CHECK(!m.bar.autohide && m.bar.showSound);
    REQUIRE(m.dock.pinned.size() == 1);   // les apps épinglées restent
    CHECK(m.dock.pinned[0].appId == L"a");
}

TEST_CASE(settings_mods_versions_and_status) {
    CHECK(md::sourceVersion("// ==WindhawkMod==\n// @id macdock-look\n// @version         1.2.0\n") == std::wstring(L"1.2.0"));
    CHECK(!md::sourceVersion("// pas de version").has_value());
    CHECK(md::compareVersions(L"1.2.0", L"1.10.0") < 0);
    CHECK(md::compareVersions(L"1.2", L"1.2.0") == 0);
    CHECK(md::compareVersions(L"2.0.0", L"1.9.9") > 0);
    using S = md::ModStatus;
    md::InstalledMod mod{L"1.1.0", false};
    CHECK(md::modStatus(false, std::nullopt, L"1.2.0") == S::WindhawkMissing);
    CHECK(md::modStatus(true, std::nullopt, L"1.2.0") == S::NotInstalled);
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::UpdateAvailable);
    mod.version = L"1.2.0";
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::UpToDate);
    mod.disabled = true;
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::Disabled);
    // La source est cherchée à côté de l'exécutable, puis dans le dépôt (build\Release → ..\..\windhawk).
    const std::wstring root = freshDir(L"mods");
    CreateDirectoryW((root + L"/build").c_str(), nullptr);
    CreateDirectoryW((root + L"/build/Release").c_str(), nullptr);
    CreateDirectoryW((root + L"/windhawk").c_str(), nullptr);
    HANDLE f = CreateFileW((root + L"/windhawk/macdock-look.wh.cpp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    CloseHandle(f);
    const std::wstring found = md::modSourcePath(root + L"/build/Release", L"macdock-look");
    CHECK(!found.empty() && GetFileAttributesW(found.c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(md::modSourcePath(root + L"/build/Release", L"inconnu").empty());
}
