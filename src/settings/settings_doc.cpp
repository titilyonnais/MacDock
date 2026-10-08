#include "settings_doc.h"

#include <windows.h>

#include <cwctype>

#include "../config/config_store.h"
#include "../core/strings.h"

namespace md {

namespace {
std::wstring dockPath(const std::wstring& dir) { return dir + L"\\settings.json"; }
std::wstring barPath(const std::wstring& dir) { return dir + L"\\menubar.json"; }

bool same(const json::Value* a, const json::Value& b) { return a && json::serialize(*a, false) == json::serialize(b, false); }

// Chemin sans guillemets ni espaces autour, barres unifiées, en minuscules : pour comparer deux commandes Run.
std::wstring normalizedPath(std::wstring v) {
    while (!v.empty() && (v.front() == L' ' || v.front() == L'"')) v.erase(v.begin());
    while (!v.empty() && (v.back() == L' ' || v.back() == L'"')) v.pop_back();
    for (wchar_t& c : v) c = c == L'/' ? L'\\' : wchar_t(towlower(c));
    return v;
}

// Démarrage actif : la valeur Run lance bien notre lanceur (une valeur d'un ancien dossier ne compte pas : la
// réactiver réécrit le bon chemin).
bool startupIsOurs(const SettingsIo* io) {
    if (!io || !io->readStartup) return false;
    const auto v = io->readStartup();
    return v && !v->empty() && normalizedPath(*v) == normalizedPath(io->launcherPath);
}

// Fichier lu (objet vide s'il manque), migré comme le Dock le fait (v1 → v2), et réglages qu'il donne. `broken` :
// invalide ou illisible, donc à ne pas réécrire.
struct DockFile {
    json::Value raw;
    Settings parsed;
    bool broken = false;
};
DockFile readDock(const std::wstring& dir) {
    LoadResult r = loadJsonFile(dockPath(dir));
    json::Value migrated = migrateSettingsJson(r.value);
    Settings parsed = settingsFromJson(migrated);
    return {std::move(migrated), std::move(parsed), r.wasInvalid || r.unreadable};
}
} // namespace

json::Value mergeChanged(json::Value file, const json::Value& before, const json::Value& after) {
    if (!after.isObject()) return file;
    if (!file.isObject()) file = json::Value(json::Object{});
    for (const auto& [key, value] : after.asObject())
        if (!same(before.find(key), value)) file.set(key, value);
    if (before.isObject())
        for (const auto& [key, value] : before.asObject())
            if (!after.find(key)) file.erase(key);
    return file;
}

SettingsModel loadModel(const std::wstring& dir, ModelFiles* status, const SettingsIo* io) {
    SettingsModel m;
    m.startup = startupIsOurs(io);
    const DockFile dock = readDock(dir);
    m.dock = dock.parsed;
    const LoadResult bar = loadJsonFile(barPath(dir));
    m.bar = menuBarSettingsFromJson(bar.value);
    if (status) *status = {dock.broken, bar.wasInvalid || bar.unreadable};
    return m;
}

bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result, const SettingsIo* io) {
    DockFile dock = readDock(dir);
    const LoadResult barFile = loadJsonFile(barPath(dir));
    const json::Value& barRaw = barFile.value;
    const bool barBroken = barFile.wasInvalid || barFile.unreadable;
    SettingsModel m{dock.parsed, menuBarSettingsFromJson(barRaw)};
    m.startup = startupIsOurs(io);
    const bool startupBefore = m.startup;
    const json::Value dockBefore = settingsToJson(m.dock), barBefore = menuBarSettingsToJson(m.bar);
    edit(m);
    const json::Value dockAfter = settingsToJson(m.dock), barAfter = menuBarSettingsToJson(m.bar);
    bool ok = true;
    if (json::serialize(dockBefore, false) != json::serialize(dockAfter, false))
        ok = !dock.broken && saveJsonFileAtomic(dockPath(dir), mergeChanged(dock.raw, dockBefore, dockAfter)) && ok;
    if (json::serialize(barBefore, false) != json::serialize(barAfter, false))
        ok = !barBroken && saveJsonFileAtomic(barPath(dir), mergeChanged(barRaw, barBefore, barAfter)) && ok;
    if (m.startup != startupBefore && io && io->writeStartup)   // guillemets, comme MacDockLauncher.exe --install
        ok = io->writeStartup(m.startup ? std::optional<std::wstring>(L"\"" + io->launcherPath + L"\"") : std::nullopt) && ok;
    if (result) *result = m;
    return ok;
}

SettingsIo registryIo(const std::wstring& launcherPath) {
    static constexpr wchar_t kRun[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    // Désactivé dans le Gestionnaire des tâches (onglet Démarrage) : premier octet impair ; Windows ne le lance pas.
    static constexpr wchar_t kApproved[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\StartupApproved\\Run";
    SettingsIo io;
    io.launcherPath = launcherPath;
    io.readStartup = []() -> std::optional<std::wstring> {
        wchar_t buf[1024] = {};
        DWORD size = sizeof buf;
        if (RegGetValueW(HKEY_CURRENT_USER, kRun, L"MacDock", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return std::nullopt;
        BYTE approved[16] = {};
        DWORD asize = sizeof approved;
        if (RegGetValueW(HKEY_CURRENT_USER, kApproved, L"MacDock", RRF_RT_REG_BINARY, nullptr, approved, &asize) == ERROR_SUCCESS &&
            asize >= 1 && (approved[0] & 1))
            return std::nullopt;
        return std::wstring(buf);
    };
    io.writeStartup = [](const std::optional<std::wstring>& v) {
        HKEY key = nullptr;   // la clé Run peut manquer sur une session neuve : créée
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRun, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
        LONG r = ERROR_SUCCESS;
        if (v) r = RegSetValueExW(key, L"MacDock", 0, REG_SZ, reinterpret_cast<const BYTE*>(v->c_str()), DWORD((v->size() + 1) * sizeof(wchar_t)));
        else RegDeleteValueW(key, L"MacDock");
        RegCloseKey(key);
        if (v) RegDeleteKeyValueW(HKEY_CURRENT_USER, kApproved, L"MacDock");   // un ancien « désactivé » ne l'emporte plus
        return r == ERROR_SUCCESS;
    };
    return io;
}

SettingsIo fileStartupIo(const std::wstring& file, const std::wstring& launcherPath) {
    SettingsIo io;
    io.launcherPath = launcherPath;
    io.readStartup = [file]() -> std::optional<std::wstring> {   // {"run": "<valeur de la clé Run>"}
        const LoadResult r = loadJsonFile(file);
        const json::Value* run = r.value.find("run");
        if (!run || !run->isString()) return std::nullopt;
        return fromUtf8(run->asString(""));
    };
    io.writeStartup = [file](const std::optional<std::wstring>& v) {
        if (!v) return DeleteFileW(file.c_str()) != FALSE || GetLastError() == ERROR_FILE_NOT_FOUND;
        json::Value doc(json::Object{});
        doc.set("run", json::Value(toUtf8(*v)));
        return saveJsonFileAtomic(file, doc);
    };
    return io;
}

} // namespace md
