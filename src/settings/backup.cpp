#include "backup.h"

#include <fstream>
#include <iterator>

#include "../config/config_store.h"
#include "../config/settings.h"
#include "../menubar/menubar_settings.h"

namespace md {

namespace {
std::wstring dockPath(const std::wstring& dir) { return dir + L"\\settings.json"; }
std::wstring barPath(const std::wstring& dir) { return dir + L"\\menubar.json"; }
}  // namespace

std::optional<json::Value> exportSettings(const std::wstring& dir) {
    json::Value out(json::Object{});
    out.set("macdockBackup", 1);
    const std::pair<const char*, std::wstring> files[] = {{"settings", dockPath(dir)}, {"menubar", barPath(dir)}};
    for (const auto& [key, path] : files) {
        const LoadResult r = loadJsonFile(path);
        if (r.wasInvalid || r.unreadable) return std::nullopt;
        if (r.fromFile) out.set(key, r.value);
    }
    return out;
}

std::optional<json::Value> readSettingsBackup(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::nullopt;
    std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (text.size() > 16u * 1024 * 1024) return std::nullopt;
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);   // BOM du Bloc-notes
    auto parsed = json::parse(text);
    if (!parsed || !parsed->isObject()) return std::nullopt;
    return std::move(*parsed);
}

int networkPins(const json::Value& backup) {
    const json::Value* dock = backup.find("settings");
    const json::Value* pinned = dock ? dock->find("pinned") : nullptr;
    if (!pinned || !pinned->isArray()) return 0;
    int n = 0;
    for (const json::Value& pin : pinned->asArray())
        for (const char* key : {"launch", "path"})
            if (const json::Value* v = pin.find(key); v && v->isString()) {
                const std::string s = v->asString("");
                if (s.size() >= 2 && (s[0] == '\\' || s[0] == '/') && (s[1] == '\\' || s[1] == '/')) {
                    ++n;
                    break;
                }
            }
    return n;
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
