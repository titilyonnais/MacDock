// App Réglages : démarrage avec Windows, sauvegarde (exporter, importer, défauts) et état des mods Windhawk.
#include <windows.h>

#include <algorithm>
#include <optional>
#include <string>

#include "minitest.h"
#include "../src/config/config_store.h"
#include "../src/settings/actions.h"
#include "../src/settings/backup.h"
#include "../src/settings/instance.h"
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

TEST_CASE(settings_startup_only_counts_our_launcher) {
    // Relecture du plan 42 : une valeur « MacDock » qui lance un ancien dossier s'affichait « activée », et la
    // réactiver ne réécrivait pas le chemin.
    const std::wstring dir = freshDir(L"startup-stale");
    FakeRun run;
    const md::SettingsIo io = run.io(L"C:/MacDock/MacDockLauncher.exe");
    run.value = L"\"C:/Ancien/MacDockLauncher.exe\"";
    CHECK(!md::loadModel(dir, nullptr, &io).startup);
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.startup = true; }, nullptr, &io));
    CHECK(run.value == std::optional<std::wstring>(L"\"C:/MacDock/MacDockLauncher.exe\""));
    run.value = L"c:/macdock/macdocklauncher.exe";   // sans guillemets, autre casse : c'est bien le nôtre
    CHECK(md::loadModel(dir, nullptr, &io).startup);
}

TEST_CASE(settings_backup_export_import_roundtrip) {
    const std::wstring dir = freshDir(L"export"), other = freshDir(L"import");
    md::saveJsonFileAtomic(dir + L"/settings.json", *md::json::parse(
        R"({"version":2,"tileSize":64,"pinned":[{"kind":"app","appId":"a","launch":"C:/a.exe","name":"A"}],"pinnedInitialized":true})"));
    md::saveJsonFileAtomic(dir + L"/menubar.json", *md::json::parse(R"({"autohide":true})"));
    const auto backup = md::exportSettings(dir);
    REQUIRE(backup.has_value());
    CHECK(md::isSettingsBackup(*backup));
    CHECK(md::importSettings(other, *backup) == md::ImportResult::Ok);
    const md::SettingsModel m = md::loadModel(other);
    CHECK_NEAR(m.dock.tileSize, 64.0, 1e-9);
    CHECK(m.bar.autohide);
    CHECK_EQ(m.dock.pinned.size(), std::size_t(1));
    // Autre document : refusé, rien n'est écrit.
    CHECK(md::importSettings(freshDir(L"refused"), *md::json::parse(R"({"hello":1})")) == md::ImportResult::NotABackup);
    CHECK(md::importSettings(other, *md::json::parse(R"({"macdockBackup":1,"settings":[1,2]})")) == md::ImportResult::Invalid);
}

TEST_CASE(settings_backup_refuses_broken_files_without_side_effects) {
    // Relecture du plan 42 : un settings.json invalide s'exportait en « settings »: {} (réimporté, il effaçait tout) ;
    // un fichier absent n'est pas exporté du tout.
    const std::wstring dir = freshDir(L"export-broken");
    md::saveJsonFileAtomic(dir + L"/menubar.json", *md::json::parse(R"({"autohide":true})"));
    const auto onlyBar = md::exportSettings(dir);
    REQUIRE(onlyBar.has_value());
    CHECK(onlyBar->find("settings") == nullptr);
    CHECK(onlyBar->find("menubar") != nullptr);
    {
        FILE* f = nullptr;
        _wfopen_s(&f, (dir + L"/settings.json").c_str(), L"wb");
        REQUIRE(f != nullptr);
        fputs("{ pas du json", f);
        fclose(f);
    }
    CHECK(!md::exportSettings(dir).has_value());
    // Lire une sauvegarde à importer ne laisse rien à côté d'elle (pas de .bak dans le dossier de l'utilisateur).
    const std::wstring junk = dir + L"/autre.json";
    {
        FILE* f = nullptr;
        _wfopen_s(&f, junk.c_str(), L"wb");
        REQUIRE(f != nullptr);
        fputs("[1, 2", f);
        fclose(f);
    }
    DeleteFileW((junk + L".bak").c_str());
    CHECK(!md::readSettingsBackup(junk).has_value());
    CHECK(GetFileAttributesW((junk + L".bak").c_str()) == INVALID_FILE_ATTRIBUTES);
    // Épingles vers le réseau (\serveur\…) : comptées, pour prévenir avant l'importation.
    const md::json::Value net = *md::json::parse(
        R"({"macdockBackup":1,"settings":{"pinned":[{"kind":"app","launch":"\\\\srv\\x.exe"},{"kind":"app","launch":"C:/a.exe"}]}})");
    CHECK_EQ(md::networkPins(net), 1);
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
    // Relecture du plan 42 : un suffixe (« -beta ») ou une espace bouclaient sans fin. Une préversion passe avant la
    // version elle-même ; les espaces autour sont ignorées.
    CHECK(md::compareVersions(L"1.4.0-beta", L"1.4.0") < 0);
    CHECK(md::compareVersions(L"1.4.0", L"1.4.0-beta") > 0);
    CHECK(md::compareVersions(L"1.3.0 ", L"1.3.0") == 0);
    CHECK(md::compareVersions(L"v2", L"1.0") != 2);   // pas de chiffre au début : préversion de 0, sans boucler
    CHECK(md::compareVersions(L"1.2.x", L"1.2.0") < 0);
    using S = md::ModStatus;
    md::InstalledMod mod{L"1.1.0", false};
    CHECK(md::modStatus(false, std::nullopt, L"1.2.0") == S::WindhawkMissing);
    CHECK(md::modStatus(true, std::nullopt, L"1.2.0") == S::NotInstalled);
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::UpdateAvailable);
    mod.version = L"1.2.0";
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::UpToDate);
    mod.disabled = true;
    CHECK(md::modStatus(true, mod, L"1.2.0") == S::Disabled);
    // Désactivé dans Windhawk avec une version plus récente livrée : la mise à jour passe devant (essai réel de la nuit
    // du 9 octobre : « Réinstaller… » était proposé à la place de « Mettre à jour… »).
    mod.version = L"1.2.0";
    CHECK(md::modStatus(true, mod, L"1.3.0") == S::UpdateAvailable);
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

TEST_CASE(settings_test_mode_keeps_startup_in_a_file) {
    // Mode d'essai (--data) : le démarrage avec Windows est gardé dans un fichier du dossier d'essai, jamais dans le
    // vrai registre.
    const std::wstring dir = freshDir(L"startup-file");
    const std::wstring file = dir + L"\\startup.txt";
    DeleteFileW(file.c_str());
    const md::SettingsIo io = md::fileStartupIo(file, L"C:\\MacDock\\MacDockLauncher.exe");
    CHECK(!io.readStartup());
    REQUIRE(io.writeStartup(std::wstring(L"\"C:\\MacDock\\MacDockLauncher.exe\"")));
    REQUIRE(io.readStartup().has_value());
    CHECK(*io.readStartup() == L"\"C:\\MacDock\\MacDockLauncher.exe\"");
    REQUIRE(io.writeStartup(std::nullopt));
    CHECK(!io.readStartup());
    // Le modèle passe par lui comme par le registre.
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.startup = true; }, nullptr, &io));
    CHECK(md::loadModel(dir, nullptr, &io).startup);
}

TEST_CASE(settings_actions_build_their_commands) {
    const std::wstring exe = L"C:\\MacDock\\build\\Release";
    md::ButtonContext ctx{exe, L"C:\\Users\\x\\AppData\\Roaming\\MacDock"};
    const auto quit = md::actionCommands(md::PaneAction::Quit, ctx);
    REQUIRE(quit.size() == 1);
    CHECK(quit[0].file == exe + L"\\MacDock.exe");
    CHECK(quit[0].params == L"--quit");
    CHECK(quit[0].wait);
    CHECK(quit[0].waitStopped);   // relecture : --quit rend la main avant que MacDock soit arrêté
    const auto restart = md::actionCommands(md::PaneAction::Restart, ctx);   // quitter, attendre, relancer
    REQUIRE(restart.size() == 2);
    CHECK(restart[0].params == L"--quit");
    CHECK(restart[1].file == exe + L"\\MacDockLauncher.exe");
    ctx.arg = L"macdock-look";
    ctx.restartExplorer = true;
    const auto install = md::actionCommands(md::PaneAction::InstallMod, ctx);
    REQUIRE(!install.empty());
    CHECK(install[0].file == L"powershell.exe");   // sans dossier système connu : le nom seul
    CHECK(install[0].verb == L"runas");   // Windows demande l'autorisation administrateur
    CHECK(install[0].wait);
    CHECK(install[0].params.find(L"install-macdock-look.ps1") != std::wstring::npos);
    // L'Explorateur est redémarré par l'app, dans la session de l'utilisateur et sans droits élevés, une fois
    // l'installateur fini : jamais par le PowerShell administrateur (relecture du plan 42).
    CHECK(install[0].params.find(L"-NoRestart") != std::wstring::npos);
    REQUIRE(install.size() == 2);
    CHECK(install[1].restartExplorer);
    ctx.systemDir = L"C:\\Windows\\System32";
    ctx.windowsDir = L"C:\\Windows";
    CHECK(md::actionCommands(md::PaneAction::InstallMod, ctx)[0].file == L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    CHECK(md::actionCommands(md::PaneAction::ShowFolder, ctx)[0].file == L"C:\\Windows\\explorer.exe");
    ctx.restartExplorer = false;
    ctx.systemDir.clear();
    ctx.windowsDir.clear();
    const auto uninstall = md::actionCommands(md::PaneAction::UninstallMod, ctx);
    REQUIRE(uninstall.size() == 1);
    CHECK(uninstall[0].params.find(L"-Uninstall") != std::wstring::npos);
    CHECK(uninstall[0].params.find(L"-NoRestart") != std::wstring::npos);
    const auto logs = md::actionCommands(md::PaneAction::ShowLogs, ctx);
    REQUIRE(logs.size() == 1);
    CHECK(logs[0].params.find(L"\\logs") != std::wstring::npos);
    CHECK(md::actionCommands(md::PaneAction::Export, ctx).empty());   // fait dans la fenêtre (dialogue de fichier)
    CHECK(md::actionCommands(md::PaneAction::InstallMod, md::ButtonContext{exe, L"", L"inconnu"}).empty());
    const auto& mutexes = md::macdockMutexes();   // ce qui doit avoir disparu avant de relancer
    CHECK(std::find(mutexes.begin(), mutexes.end(), L"Local\\MacDockLauncher") != mutexes.end());
    CHECK(std::find(mutexes.begin(), mutexes.end(), L"Local\\MacMenuBar") != mutexes.end());
}

TEST_CASE(settings_test_instance_never_talks_to_the_real_one) {
    const md::InstanceNames real = md::settingsInstance(false), test = md::settingsInstance(true);
    CHECK(!real.mutex.empty());                    // la vraie app : une seule instance
    CHECK(test.mutex.empty());                     // un essai : ni mutex ni transmission
    CHECK(real.windowClass != test.windowClass);   // une seconde ouverture ne trouve jamais l'autre
    CHECK(real.windowClass == L"MacDockSettingsWindow");
}
