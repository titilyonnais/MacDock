#include "settings_doc.h"

#include "../config/config_store.h"

namespace md {

namespace {
std::wstring dockPath(const std::wstring& dir) { return dir + L"\\settings.json"; }
std::wstring barPath(const std::wstring& dir) { return dir + L"\\menubar.json"; }

bool same(const json::Value* a, const json::Value& b) { return a && json::serialize(*a, false) == json::serialize(b, false); }

// Fichier lu tel quel (objet vide s'il manque) et réglages qu'il donne, comme le Dock les lit (migration v1 comprise).
struct DockFile {
    json::Value raw;
    Settings parsed;
};
DockFile readDock(const std::wstring& dir) {
    LoadResult r = loadJsonFile(dockPath(dir));
    return {r.value, settingsFromJson(migrateSettingsJson(r.value))};
}
} // namespace

json::Value mergeChanged(json::Value file, const json::Value& before, const json::Value& after) {
    if (!after.isObject()) return file;
    if (!file.isObject()) file = json::Value(json::Object{});
    for (const auto& [key, value] : after.asObject())
        if (!same(before.find(key), value)) file.set(key, value);
    return file;
}

SettingsModel loadModel(const std::wstring& dir) {
    SettingsModel m;
    m.dock = readDock(dir).parsed;
    m.bar = menuBarSettingsFromJson(loadJsonFile(barPath(dir)).value);
    return m;
}

bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result) {
    DockFile dock = readDock(dir);
    const json::Value barRaw = loadJsonFile(barPath(dir)).value;
    SettingsModel m{dock.parsed, menuBarSettingsFromJson(barRaw)};
    const json::Value dockBefore = settingsToJson(m.dock), barBefore = menuBarSettingsToJson(m.bar);
    edit(m);
    const json::Value dockAfter = settingsToJson(m.dock), barAfter = menuBarSettingsToJson(m.bar);
    bool ok = true;
    if (json::serialize(dockBefore, false) != json::serialize(dockAfter, false))
        ok = saveJsonFileAtomic(dockPath(dir), mergeChanged(dock.raw, dockBefore, dockAfter)) && ok;
    if (json::serialize(barBefore, false) != json::serialize(barAfter, false))
        ok = saveJsonFileAtomic(barPath(dir), mergeChanged(barRaw, barBefore, barAfter)) && ok;
    if (result) *result = m;
    return ok;
}

} // namespace md
