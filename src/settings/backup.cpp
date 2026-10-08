#include "backup.h"

#include "../config/config_store.h"
#include "../config/settings.h"
#include "../menubar/menubar_settings.h"

namespace md {

namespace {
std::wstring dockPath(const std::wstring& dir) { return dir + L"\\settings.json"; }
std::wstring barPath(const std::wstring& dir) { return dir + L"\\menubar.json"; }
}  // namespace

json::Value exportSettings(const std::wstring& dir) {
    json::Value out(json::Object{});
    out.set("macdockBackup", 1);
    out.set("settings", loadJsonFile(dockPath(dir)).value);
    out.set("menubar", loadJsonFile(barPath(dir)).value);
    return out;
}

bool isSettingsBackup(const json::Value& v) { return v.isObject() && v.find("macdockBackup") != nullptr; }

ImportResult importSettings(const std::wstring& dir, const json::Value& backup) {
    if (!isSettingsBackup(backup)) return ImportResult::NotABackup;
    const json::Value* dock = backup.find("settings");
    const json::Value* bar = backup.find("menubar");
    if ((dock && !dock->isObject()) || (bar && !bar->isObject()) || (!dock && !bar)) return ImportResult::Invalid;
    bool ok = true;
    if (dock) ok = saveJsonFileAtomic(dockPath(dir), migrateSettingsJson(*dock)) && ok;
    if (bar) ok = saveJsonFileAtomic(barPath(dir), *bar) && ok;
    return ok ? ImportResult::Ok : ImportResult::WriteFailed;
}

bool resetSettings(const std::wstring& dir) {
    const LoadResult current = loadJsonFile(dockPath(dir));
    if (current.wasInvalid || current.unreadable) return false;   // ses épingles ne se liraient pas : on n'y touche pas
    const Settings old = settingsFromJson(migrateSettingsJson(current.value));
    Settings fresh;
    fresh.pinned = old.pinned;
    fresh.pinnedInitialized = old.pinnedInitialized;
    return saveJsonFileAtomic(dockPath(dir), settingsToJson(fresh)) &&
           saveJsonFileAtomic(barPath(dir), menuBarSettingsToJson(MenuBarSettings{}));
}

}  // namespace md
