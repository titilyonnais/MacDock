#include "settings_doc.h"

#include <windows.h>

#include "../config/config_store.h"

namespace md {

namespace {
std::wstring dockPath(const std::wstring& dir) { return dir + L"\\settings.json"; }
std::wstring barPath(const std::wstring& dir) { return dir + L"\\menubar.json"; }

bool same(const json::Value* a, const json::Value& b) { return a && json::serialize(*a, false) == json::serialize(b, false); }

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
    if (io && io->readStartup) {
        const auto v = io->readStartup();
        m.startup = v && !v->empty();
    }
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
    if (io && io->readStartup) {
        const auto v = io->readStartup();
        m.startup = v && !v->empty();
    }
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
    SettingsIo io;
    io.launcherPath = launcherPath;
    io.readStartup = []() -> std::optional<std::wstring> {
        wchar_t buf[1024] = {};
        DWORD size = sizeof buf;
        if (RegGetValueW(HKEY_CURRENT_USER, kRun, L"MacDock", RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) return std::nullopt;
        return std::wstring(buf);
    };
    io.writeStartup = [](const std::optional<std::wstring>& v) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRun, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
        LONG r = ERROR_SUCCESS;
        if (v) r = RegSetValueExW(key, L"MacDock", 0, REG_SZ, reinterpret_cast<const BYTE*>(v->c_str()), DWORD((v->size() + 1) * sizeof(wchar_t)));
        else RegDeleteValueW(key, L"MacDock");
        RegCloseKey(key);
        return r == ERROR_SUCCESS;
    };
    return io;
}

} // namespace md
