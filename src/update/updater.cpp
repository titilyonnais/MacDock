#include "updater.h"

#include <windows.h>
#include <shlobj.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

#include "../core/json.h"
#include "../core/log.h"
#include "../core/strings.h"
#include "../core/version.h"
#include "release_key.h"
#include "update_net.h"
#include "update_sign.h"

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "advapi32.lib")

namespace md::update {

namespace {

constexpr std::size_t kMaxApiBytes = 2 * 1024 * 1024;            // réponse de l'API
constexpr std::uint64_t kMaxInstallerBytes = 200ull * 1024 * 1024;
constexpr std::size_t kMaxSumsBytes = 64 * 1024;
constexpr std::size_t kMaxSigBytes = 4 * 1024;

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::wstring s;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &p))) s = p;
    CoTaskMemFree(p);
    return s;
}

bool exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool cancelled(const Options& o) { return o.cancel && WaitForSingleObject(static_cast<HANDLE>(o.cancel), 0) == WAIT_OBJECT_0; }

std::int64_t nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Un seul processus à la fois : le lanceur (fil de fond) et l'app Réglages (« Rechercher maintenant »). L'installateur
// attend aussi que ce verrou disparaisse. `cancel` levé pendant l'attente : pas de verrou.
struct Lock {
    HANDLE m = CreateMutexW(nullptr, FALSE, L"Local\\MacDockUpdate");
    bool held = false;
    explicit Lock(DWORD waitMs, HANDLE cancel = nullptr) {
        if (!m) return;
        const HANDLE waits[] = {m, cancel};
        const DWORD r = WaitForMultipleObjects(cancel ? 2 : 1, waits, FALSE, waitMs);
        held = r == WAIT_OBJECT_0 || r == WAIT_ABANDONED_0;   // abandonné : un processus arrêté en cours de route
    }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    ~Lock() {
        if (held) ReleaseMutex(m);
        if (m) CloseHandle(m);
    }
};

void createDirs(const std::wstring& dir) {
    for (std::size_t at = dir.find_first_of(L"\\/", 3); !dir.empty(); at = dir.find_first_of(L"\\/", at + 1)) {
        CreateDirectoryW(dir.substr(0, at).c_str(), nullptr);
        if (at == std::wstring::npos) break;
    }
}

// Installateurs d'autres versions laissés dans le dossier : effacés.
void cleanDownloads(const std::wstring& dir, const std::wstring& keep) {
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((dir + L"\\MacDock-Setup-*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        const std::wstring path = dir + L"\\" + fd.cFileName;
        if (_wcsicmp(path.c_str(), keep.c_str()) != 0) DeleteFileW(path.c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// cmd.exe interdit par une stratégie (DisableCMD) : pas de relais.
bool cmdAllowed() {
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        DWORD value = 0, size = sizeof value;
        if (RegGetValueW(root, L"Software\\Policies\\Microsoft\\Windows\\System", L"DisableCMD", RRF_RT_REG_DWORD, nullptr,
                         &value, &size) == ERROR_SUCCESS && value != 0)
            return false;
    }
    return true;
}

// Relais du démarrage : cmd.exe, sans fenêtre, lance l'installateur, attend sa fin, puis relance ce lanceur depuis son
// dossier. Les chemins passent par l'environnement du relais : cmd ne les réinterprète jamais (« % », « & »…).
bool startRelay(const std::wstring& setup, const std::wstring& logFile, const std::wstring& workDir, PROCESS_INFORMATION* pi) {
    wchar_t sys[MAX_PATH] = {}, self[MAX_PATH] = {};
    if (!GetSystemDirectoryW(sys, MAX_PATH) || !GetModuleFileNameW(nullptr, self, MAX_PATH)) return false;
    std::wstring selfDir(self);
    selfDir.resize(selfDir.find_last_of(L'\\'));
    const std::pair<const wchar_t*, std::wstring> vars[] = {
        {L"MACDOCK_RELAY_SETUP", setup}, {L"MACDOCK_RELAY_LOG", logFile}, {L"MACDOCK_RELAY_DIR", selfDir}, {L"MACDOCK_RELAY_LAUNCHER", self}};
    for (const auto& [name, value] : vars) SetEnvironmentVariableW(name, value.c_str());
    const std::wstring cmdExe = std::wstring(sys) + L"\\cmd.exe";
    std::wstring cmd = L"\"" + cmdExe +
                       L"\" /d /v:off /c start \"\" /wait \"%MACDOCK_RELAY_SETUP%\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- "
                       L"/LOG=\"%MACDOCK_RELAY_LOG%\" /RELAUNCH & start \"\" /d \"%MACDOCK_RELAY_DIR%\" \"%MACDOCK_RELAY_LAUNCHER%\"";
    STARTUPINFOW si{sizeof si};
    const bool ok = CreateProcessW(cmdExe.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, pi) != FALSE;
    for (const auto& [name, value] : vars) SetEnvironmentVariableW(name, nullptr);
    return ok;
}

} // namespace

Paths defaultPaths() {
    wchar_t* v = nullptr;
    std::size_t n = 0;
    std::wstring dir;
    if (_wdupenv_s(&v, &n, L"MACDOCK_UPDATE_DIR") == 0 && v) dir = v;
    std::free(v);
    if (!dir.empty()) return {dir + L"\\update.json", dir + L"\\updates"};
    return {knownFolder(FOLDERID_RoamingAppData) + L"\\MacDock\\update.json", knownFolder(FOLDERID_LocalAppData) + L"\\MacDock\\updates"};
}

Options optionsFromEnvironment() {
    Options o;
    wchar_t* v = nullptr;
    std::size_t n = 0;
    if (_wdupenv_s(&v, &n, L"MACDOCK_UPDATE_PRERELEASE") == 0 && v && wcscmp(v, L"1") == 0) o.prerelease = true;
    std::free(v);
    return o;
}

UpdateState load(const Paths& p) {
    std::ifstream f(p.stateFile, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return parseUpdateState(ss.str());
}

bool save(const Paths& p, const UpdateState& s) {
    const std::wstring dir = p.stateFile.substr(0, p.stateFile.find_last_of(L"\\/"));
    createDirs(dir);
    // « Rechercher automatiquement » appartient à l'app Réglages : celui du fichier est gardé (il a pu changer pendant
    // une recherche, entre la lecture de l'état et son écriture).
    UpdateState out = s;
    {
        std::ifstream f(p.stateFile, std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        if (const auto doc = json::parse(ss.str()); doc && doc->isObject())
            if (const json::Value* v = doc->find("automatic")) out.automatic = v->asBool(out.automatic);
    }
    const std::wstring tmp = p.stateFile + L"." + std::to_wstring(GetCurrentProcessId()) + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        const std::string text = updateStateJson(out);
        f.write(text.data(), std::streamsize(text.size()));
        if (!f) return false;
    }
    if (MoveFileExW(tmp.c_str(), p.stateFile.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
    DeleteFileW(tmp.c_str());
    return false;
}

CheckResult check(const Paths& p, const Options& o, std::wstring* readyOut) {
    Lock lock(o.lockWaitMs, static_cast<HANDLE>(o.cancel));
    if (cancelled(o)) return CheckResult::Failed;   // arrêt de MacDock : rien n'est touché
    if (!lock.held) {   // un autre processus cherche ou installe encore : rien n'est touché
        log::warn(L"Mise à jour : une autre recherche est en cours, celle-ci est abandonnée");
        return CheckResult::Failed;
    }
    UpdateState s = load(p);
    s.lastCheck = nowSeconds();
    const auto current = parseVersion(kMacDockVersion);
    const std::wstring api = releasesUrl(o.repo);   // préversions écartées par pickUpdate, sauf o.prerelease
    std::wstring error;
    const auto body = httpsGet(api, kMaxApiBytes, L"application/vnd.github+json", &error);
    if (!body || !current) {
        s.lastError = body ? L"version courante illisible" : error;
        save(p, s);
        log::warn(L"Mise à jour : recherche impossible (%s)", s.lastError.c_str());
        return CheckResult::Failed;
    }
    if (cancelled(o)) return CheckResult::Failed;
    const auto offer = pickUpdate(parseReleases(*body), *current, o.prerelease);
    if (!offer) {
        s.lastError.clear();
        save(p, s);
        log::info(L"Mise à jour : MacDock %s est à jour", kMacDockVersion);
        return CheckResult::UpToDate;
    }
    const std::wstring version = versionText(offer->version);
    const std::wstring path = p.downloadDir + L"\\" + offer->installer.name;
    // Déjà téléchargée et intacte : rien à refaire.
    if (s.readyVersion == version && s.readyPath == path && !s.readySha256.empty() && sha256File(path) == s.readySha256) {
        s.lastError.clear();
        save(p, s);
        if (readyOut) *readyOut = version;
        return CheckResult::Ready;
    }
    // Signature d'abord : une version que la clé des versions n'a pas signée n'est jamais téléchargée.
    const auto sums = httpsGet(offer->sums.url, kMaxSumsBytes, L"application/octet-stream", &error);
    const auto sig = sums ? httpsGet(offer->signature.url, kMaxSigBytes, L"application/octet-stream", &error) : std::nullopt;
    if (!sums || !sig) {
        s.lastError = error;
        save(p, s);
        log::warn(L"Mise à jour %s : %s", version.c_str(), s.lastError.c_str());
        return CheckResult::Failed;
    }
    if (!verifyReleaseSignature(*sums, *sig, releasePublicKey())) {
        s.lastError = L"signature invalide : version refusée";
        save(p, s);
        log::error(L"Mise à jour %s : signature invalide, version refusée", version.c_str());
        return CheckResult::Failed;
    }
    const auto expected = expectedSha256(*sums, offer->installer.name);
    if (!expected) {
        s.lastError = L"empreinte absente de SHA256SUMS.txt";
        save(p, s);
        log::warn(L"Mise à jour %s : %s", version.c_str(), s.lastError.c_str());
        return CheckResult::Failed;
    }
    if (cancelled(o)) return CheckResult::Failed;
    createDirs(p.downloadDir);
    if (!httpsDownload(offer->installer.url, path, std::min<std::uint64_t>(kMaxInstallerBytes, offer->installer.size + 1), &error,
                       o.cancel)) {
        if (cancelled(o)) return CheckResult::Failed;
        s.lastError = error;
        save(p, s);
        log::warn(L"Mise à jour %s : téléchargement impossible (%s)", version.c_str(), error.c_str());
        return CheckResult::Failed;
    }
    const std::string got = sha256File(path);
    if (got != *expected) {   // jamais exécuté : effacé
        DeleteFileW(path.c_str());
        s.lastError = L"empreinte différente : installateur refusé";
        save(p, s);
        log::error(L"Mise à jour %s : empreinte différente, installateur effacé", version.c_str());
        return CheckResult::Failed;
    }
    cleanDownloads(p.downloadDir, path);
    s.readyVersion = version;
    s.readyPath = path;
    s.readySha256 = got;
    s.lastError.clear();
    save(p, s);
    log::info(L"Mise à jour : MacDock %s téléchargée et vérifiée", version.c_str());
    if (readyOut) *readyOut = version;
    return CheckResult::Ready;
}

bool launchInstaller(const Paths& p, bool relaunch, unsigned lockWaitMs) {
    Lock lock(lockWaitMs);
    if (!lock.held) {
        log::warn(L"Mise à jour : une recherche est en cours, installation remise à plus tard");
        return false;
    }
    UpdateState s = load(p);
    if (s.readyVersion.empty() || !exists(s.readyPath) || sha256File(s.readyPath) != s.readySha256) {
        log::warn(L"Mise à jour : aucun installateur prêt et intact");
        return false;
    }
    const std::wstring logFile = p.downloadDir + L"\\install.log";
    std::wstring cmd = L"\"" + s.readyPath + L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- /LOG=\"" + logFile + L"\"";
    if (relaunch) cmd += L" /RELAUNCH";
    s.attemptedVersion = s.readyVersion;   // une seule tentative par version
    save(p, s);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    // Au démarrage, rien ne tourne : un relais attend la fin de l'installateur puis relance ce lanceur, même si
    // l'installateur s'est arrêté tôt (autre installation en cours, journal impossible, antivirus) ; s'il a déjà relancé
    // MacDock, le second lanceur trouve le premier et sort aussitôt.
    bool started = relaunch && cmdAllowed() &&
                   startRelay(s.readyPath, logFile, p.downloadDir, &pi);
    if (!started)
        started = CreateProcessW(s.readyPath.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, p.downloadDir.c_str(), &si, &pi) != FALSE;
    if (!started) {
        log::error(L"Mise à jour : installateur impossible à lancer (%lu)", GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    log::info(L"Mise à jour : installation de MacDock %s lancée", s.readyVersion.c_str());
    return true;
}

bool runsFromInstalledCopy() {
    wchar_t self[MAX_PATH] = {}, location[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, self, MAX_PATH)) return false;
    std::wstring dir(self);
    dir.resize(dir.find_last_of(L'\\'));
    DWORD size = sizeof location;
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{7C2E4C1B-8A3D-4F6E-9B21-5D0A3C9E7F14}_is1",
                     L"InstallLocation", RRF_RT_REG_SZ, nullptr, location, &size) != ERROR_SUCCESS)
        return false;
    return installedCopy(dir, location);
}

bool installAtStartup(const Paths& p) {
    Lock lock(5000);   // une recherche lancée à la main (Réglages) : le démarrage n'attend pas, rien n'est touché
    if (!lock.held) return false;
    UpdateState s = load(p);
    const auto current = parseVersion(kMacDockVersion);
    if (!current) return false;
    switch (startupAction(s, *current, exists(s.readyPath))) {
        case StartupAction::Install: return launchInstaller(p, true);
        case StartupAction::DropAttempt:
            log::warn(L"Mise à jour %s : l'installation précédente n'a pas pris, abandonnée", s.readyVersion.c_str());
            [[fallthrough]];
        case StartupAction::DropStale:
            if (!s.readyPath.empty()) DeleteFileW(s.readyPath.c_str());
            s.readyVersion.clear();
            s.readyPath.clear();
            s.readySha256.clear();
            s.attemptedVersion.clear();
            save(p, s);
            return false;
        case StartupAction::None: return false;
    }
    return false;
}

} // namespace md::update
