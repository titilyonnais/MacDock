// Mises à jour (plan 53), orchestration sans réseau : état gardé, installateur altéré jamais lancé, version périmée ou
// tentative ratée oubliées au démarrage. Tout se passe dans un dossier temporaire (jamais %APPDATA%), et aucun
// installateur n'est lancé.
#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

#include "minitest.h"
#include "../src/core/version.h"
#include "../src/update/update_net.h"
#include "../src/launcher/update_notifier.h"
#include "../src/update/updater.h"

namespace {

struct TempDir {
    std::wstring path;
    TempDir() {
        wchar_t tmp[MAX_PATH] = {};
        GetTempPathW(MAX_PATH, tmp);
        path = std::wstring(tmp) + L"macdock-updater-test-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(path.c_str(), nullptr);
    }
    ~TempDir() {
        for (const wchar_t* f : {L"\\update.json", L"\\updates\\MacDock-Setup-9.9.9.exe", L"\\updates\\MacDock-Setup-0.1.0.exe"})
            DeleteFileW((path + f).c_str());
        RemoveDirectoryW((path + L"\\updates").c_str());
        RemoveDirectoryW(path.c_str());
    }
    md::update::Paths paths() const { return {path + L"\\update.json", path + L"\\updates"}; }
};

void write(const std::wstring& path, const char* text) {
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") == 0 && f) {
        fputs(text, f);
        fclose(f);
    }
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

std::string readAll(const std::wstring& path) {
    std::string out;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") == 0 && f) {
        char buf[4096];
        for (std::size_t n; (n = fread(buf, 1, sizeof buf, f)) > 0;) out.append(buf, n);
        fclose(f);
    }
    return out;
}

// Un autre processus tient le verrou des mises à jour (ici un autre fil : un mutex appartient à un fil).
struct HeldUpdateLock {
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread holder;
    HeldUpdateLock() {
        holder = std::thread([this] {
            HANDLE m = CreateMutexW(nullptr, FALSE, L"Local\\MacDockUpdate");
            WaitForSingleObject(m, INFINITE);
            SetEvent(ready);
            WaitForSingleObject(done, INFINITE);
            ReleaseMutex(m);
            CloseHandle(m);
        });
        WaitForSingleObject(ready, 10000);
    }
    ~HeldUpdateLock() {
        SetEvent(done);
        holder.join();
        CloseHandle(ready);
        CloseHandle(done);
    }
};

} // namespace

TEST_CASE(updater_save_keeps_automatic_from_file) {   // relecture du plan 53, important 7
    // « Rechercher automatiquement » appartient à l'app Réglages : coupé pendant une recherche, il n'est pas rallumé
    // par l'état que le lanceur réécrit à la fin.
    TempDir dir;
    const auto paths = dir.paths();
    md::UpdateState settings;
    settings.automatic = false;
    REQUIRE(md::update::save(paths, settings));   // fichier neuf : écrit tel quel
    md::UpdateState launcher;                     // lu au début de la recherche, quand c'était encore oui
    launcher.automatic = true;
    launcher.lastCheck = 1791500000;
    REQUIRE(md::update::save(paths, launcher));
    const md::UpdateState back = md::update::load(paths);
    CHECK(!back.automatic);
    CHECK(back.lastCheck == 1791500000);
}

TEST_CASE(updater_busy_lock_changes_nothing) {   // relecture du plan 53, important 7
    // Verrou toujours pris après l'attente : ni installateur lancé, ni état réécrit (deux recherches à la fois
    // effaceraient la version prête notée par l'autre).
    TempDir dir;
    const auto paths = dir.paths();
    CreateDirectoryW(paths.downloadDir.c_str(), nullptr);
    const std::wstring installer = paths.downloadDir + L"\\MacDock-Setup-9.9.9.exe";
    write(installer, "installateur vérifié");
    md::UpdateState s;
    s.readyVersion = L"9.9.9";
    s.readyPath = installer;
    s.readySha256 = md::sha256Hex("installateur vérifié");
    REQUIRE(md::update::save(paths, s));
    const std::string before = readAll(paths.stateFile);
    HeldUpdateLock busy;
    CHECK(!md::update::launchInstaller(paths, false, 200));
    md::update::Options o;
    o.repo = std::wstring(3000, L'x');   // adresse refusée avant tout accès au réseau, si la recherche partait quand même
    o.lockWaitMs = 200;
    CHECK(md::update::check(paths, o) == md::update::CheckResult::Failed);
    CHECK(readAll(paths.stateFile) == before);
}

TEST_CASE(updater_state_file_round_trip) {
    TempDir dir;
    const auto paths = dir.paths();
    CHECK(md::update::load(paths).automatic);   // pas de fichier : valeurs par défaut
    md::UpdateState s;
    s.automatic = false;
    s.readyVersion = L"9.9.9";
    s.lastError = L"hors ligne ou GitHub injoignable";
    REQUIRE(md::update::save(paths, s));
    const md::UpdateState back = md::update::load(paths);
    CHECK(!back.automatic);
    CHECK(back.readyVersion == L"9.9.9");
    CHECK(back.lastError == s.lastError);
}

TEST_CASE(updater_never_launches_tampered_installer) {
    TempDir dir;
    const auto paths = dir.paths();
    CreateDirectoryW(paths.downloadDir.c_str(), nullptr);
    const std::wstring installer = paths.downloadDir + L"\\MacDock-Setup-9.9.9.exe";
    write(installer, "pas l'installateur vérifié");
    md::UpdateState s;
    s.readyVersion = L"9.9.9";
    s.readyPath = installer;
    s.readySha256 = md::sha256Hex("l'installateur vérifié");   // empreinte de ce qui avait été vérifié
    REQUIRE(md::update::save(paths, s));
    CHECK(!md::update::launchInstaller(paths, false));
    CHECK(md::update::load(paths).attemptedVersion.empty());   // aucune tentative notée
}

TEST_CASE(updater_startup_forgets_stale_and_failed) {
    TempDir dir;
    const auto paths = dir.paths();
    CreateDirectoryW(paths.downloadDir.c_str(), nullptr);
    // Version prête plus ancienne que celle qui tourne (installée depuis) : oubliée, installateur effacé.
    const std::wstring old = paths.downloadDir + L"\\MacDock-Setup-0.1.0.exe";
    write(old, "ancien");
    md::UpdateState s;
    s.readyVersion = L"0.1.0";
    s.readyPath = old;
    s.readySha256 = md::sha256Hex("ancien");
    REQUIRE(md::update::save(paths, s));
    CHECK(!md::update::installAtStartup(paths));
    CHECK(md::update::load(paths).readyVersion.empty());
    CHECK(!exists(old));
    // Tentative déjà faite pour cette version, et pourtant toujours l'ancienne qui tourne : abandon, pas de boucle.
    const std::wstring next = paths.downloadDir + L"\\MacDock-Setup-9.9.9.exe";
    write(next, "suivante");
    s.readyVersion = L"9.9.9";
    s.readyPath = next;
    s.readySha256 = md::sha256Hex("suivante");
    s.attemptedVersion = L"9.9.9";
    REQUIRE(md::update::save(paths, s));
    CHECK(!md::update::installAtStartup(paths));
    const md::UpdateState after = md::update::load(paths);
    CHECK(after.readyVersion.empty());
    CHECK(after.attemptedVersion.empty());
    CHECK(!exists(next));
}

namespace {

// Variable d'environnement le temps d'un test (vue du C++ comme de Windows).
struct EnvVar {
    std::wstring name;
    EnvVar(const wchar_t* n, const std::wstring& v) : name(n) { _wputenv_s(n, v.c_str()); }
    ~EnvVar() { _wputenv_s(name.c_str(), L""); }
};

// Exécute `f` dans un fil, au plus `ms` : false s'il bloque encore (il est alors laissé là et finit avec les tests).
template <class F>
bool finishesWithin(unsigned ms, F f) {
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    std::thread([f, done] {
        f();
        SetEvent(done);
    }).detach();
    const bool ok = WaitForSingleObject(done, ms) == WAIT_OBJECT_0;
    if (ok) CloseHandle(done);   // sinon le fil s'en sert encore
    return ok;
}

} // namespace

TEST_CASE(update_notifier_stops_right_after_start) {   // relecture du plan 53, important 4
    // Ni Dock ni barre lancés (fichiers absents) : la boucle du lanceur se vide aussitôt, l'arrêt suit le départ du fil.
    CHECK(finishesWithin(10000, [] {
        for (int i = 0; i < 40; ++i) {
            md::UpdateNotifier n;
            n.start(GetModuleHandleW(nullptr));
            n.stop();
        }
    }));
}

TEST_CASE(update_notifier_stop_interrupts_waiting_check) {   // relecture du plan 53, important 5
    // Recherche bloquée (ici sur le verrou, qu'un autre processus tient) : l'arrêt du lanceur ne l'attend pas, sinon
    // l'installateur abandonne au bout de 30 s et MacDock ne revient pas.
    TempDir dir;
    EnvVar updateDir(L"MACDOCK_UPDATE_DIR", dir.path);
    EnvVar delay(L"MACDOCK_UPDATE_DELAY", L"1");
    auto* busy = new HeldUpdateLock;   // jamais rendu si l'arrêt bloque : la recherche ne part alors jamais sur le réseau
    auto* n = new md::UpdateNotifier;
    n->start(GetModuleHandleW(nullptr));
    Sleep(1600);   // première recherche une seconde après le départ : bloquée sur le verrou
    const bool stopped = finishesWithin(5000, [n] { n->stop(); });
    CHECK(stopped);
    if (stopped) {
        delete n;
        delete busy;
    }
    CHECK(!exists(dir.path + L"\\update.json"));   // rien d'écrit
}
