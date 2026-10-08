#include "settings_doc.h"

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

SettingsModel loadModel(const std::wstring& dir, ModelFiles* status) {
    SettingsModel m;
    const DockFile dock = readDock(dir);
    m.dock = dock.parsed;
    const LoadResult bar = loadJsonFile(barPath(dir));
    m.bar = menuBarSettingsFromJson(bar.value);
    if (status) *status = {dock.broken, bar.wasInvalid || bar.unreadable};
    return m;
}

bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result) {
    DockFile dock = readDock(dir);
    const LoadResult barFile = loadJsonFile(barPath(dir));
    const json::Value& barRaw = barFile.value;
    const bool barBroken = barFile.wasInvalid || barFile.unreadable;
    SettingsModel m{dock.parsed, menuBarSettingsFromJson(barRaw)};
    const json::Value dockBefore = settingsToJson(m.dock), barBefore = menuBarSettingsToJson(m.bar);
    edit(m);
    const json::Value dockAfter = settingsToJson(m.dock), barAfter = menuBarSettingsToJson(m.bar);
    bool ok = true;
    if (json::serialize(dockBefore, false) != json::serialize(dockAfter, false))
        ok = !dock.broken && saveJsonFileAtomic(dockPath(dir), mergeChanged(dock.raw, dockBefore, dockAfter)) && ok;
    if (json::serialize(barBefore, false) != json::serialize(barAfter, false))
        ok = !barBroken && saveJsonFileAtomic(barPath(dir), mergeChanged(barRaw, barBefore, barAfter)) && ok;
    if (result) *result = m;
    return ok;
}

} // namespace md
