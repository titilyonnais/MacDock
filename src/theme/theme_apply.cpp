#include "theme_apply.h"

#include <algorithm>

#include "../calib/png_io.h"
#include "../core/strings.h"
#include "cursor_art.h"
#include "cursor_file.h"
#include "wallpaper_art.h"

namespace md {

namespace {

constexpr int kCursorSizes[] = {32, 48, 64, 96, 128};
constexpr int kAniJiffies = 5;   // 12 images × 5/60 s : un tour en une seconde

std::vector<std::uint8_t> cursorFile(CursorKind k) {
    if (!cursorAnimated(k)) {
        std::vector<CursorFrame> sizes;
        for (int s : kCursorSizes) sizes.push_back(cursorFrames(k, s)[0]);
        return encodeCur(sizes);
    }
    std::vector<std::vector<CursorFrame>> bySize;   // [taille][image]
    for (int s : kCursorSizes) bySize.push_back(cursorFrames(k, s));
    std::vector<std::vector<std::uint8_t>> frames;
    for (std::size_t f = 0; f < bySize[0].size(); ++f) {
        std::vector<CursorFrame> sizes;
        for (const auto& all : bySize) sizes.push_back(all[f]);
        frames.push_back(encodeCur(sizes));
    }
    return encodeAni(frames, kAniJiffies);
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') return dir + L"\\" + name;
    return dir + name;
}

void note(ThemeResult& r, const std::wstring& line) {
    if (!r.message.empty()) r.message += L"\n";
    r.message += line;
}

json::Value mapToJson(const std::map<std::wstring, std::wstring>& m) {
    json::Value v = json::Object{};
    for (const auto& [k, val] : m) v.set(toUtf8(k), json::Value(toUtf8(val)));
    return v;
}

bool mapFromJson(const json::Value* v, std::map<std::wstring, std::wstring>& out) {
    if (!v || !v->isObject()) return false;
    for (const auto& [k, val] : v->asObject()) {
        if (!val.isString()) return false;
        out[fromUtf8(k)] = fromUtf8(val.asString(""));
    }
    return true;
}

} // namespace

json::Value themeBackupToJson(const ThemeBackup& b) {
    json::Value v = json::Object{};
    v.set("cursors", mapToJson(b.cursors));
    v.set("wallpapers", mapToJson(b.wallpapers));
    return v;
}

std::optional<ThemeBackup> themeBackupFromJson(const json::Value& v) {
    ThemeBackup b;
    if (!mapFromJson(v.find("cursors"), b.cursors) || !mapFromJson(v.find("wallpapers"), b.wallpapers)) return std::nullopt;
    return b;
}

ThemeResult applyTheme(ThemeApi& api, const std::wstring& dir, std::optional<ThemeBackup>& backup) {
    ThemeResult r;
    // 1. Les fichiers d'abord : rien n'est changé si l'un d'eux ne peut pas être écrit.
    std::map<std::wstring, std::wstring> cursorPaths;
    for (CursorKind k : kThemeCursors) {
        const std::wstring path = joinPath(dir, cursorFileName(k));
        if (!api.writeFile(path, cursorFile(k))) {
            r.message = L"Impossible d'écrire " + path;
            return r;
        }
        cursorPaths[cursorRegistryName(k)] = path;
    }
    const bool dark = api.darkMode();
    const std::vector<std::wstring> screens = api.monitors();
    std::map<std::wstring, std::wstring> wallPaths;
    for (std::size_t i = 0; i < screens.size(); ++i) {
        SIZE sz = api.monitorSize(screens[i]);
        if (sz.cx <= 0 || sz.cy <= 0 || sz.cx > 16384 || sz.cy > 16384) sz = SIZE{1920, 1080};
        const BgraImage wall = tahoeWallpaper(sz.cx, sz.cy, dark);
        const std::wstring path =
            joinPath(dir, L"wallpaper-" + std::to_wstring(i + 1) + (dark ? L"-dark.png" : L"-light.png"));
        if (!api.writeFile(path, encodePng(wall.px.data(), UINT(wall.w), UINT(wall.h)))) {
            r.message = L"Impossible d'écrire " + path;
            return r;
        }
        wallPaths[screens[i]] = path;
    }
    // 2. La sauvegarde, une seule fois : une deuxième application garde l'état d'origine.
    if (!backup) {
        ThemeBackup b;
        for (CursorKind k : kThemeCursors) b.cursors[cursorRegistryName(k)] = api.readCursor(cursorRegistryName(k)).value_or(L"");
        for (const std::wstring& id : screens) b.wallpapers[id] = api.getWallpaper(id);
        if (api.saveBackup && !api.saveBackup(b)) {
            r.message = L"Impossible d'enregistrer la sauvegarde du thème Windows : rien n'a été changé";
            return r;
        }
        backup = std::move(b);
    }
    // 3. Les changements ; on va au bout et on signale ce qui a échoué.
    r.ok = true;
    for (const auto& [name, path] : cursorPaths)
        if (!api.writeCursor(name, path)) {
            r.ok = false;
            note(r, L"Curseur non changé : " + name);
        }
    if (!api.reloadCursors()) {
        r.ok = false;
        note(r, L"Windows n'a pas rechargé les curseurs");
    }
    for (const auto& [id, path] : wallPaths)
        if (!api.setWallpaper(id, path)) {
            r.ok = false;
            note(r, L"Fond d'écran non changé : " + id);
        }
    return r;
}

ThemeResult restoreTheme(ThemeApi& api, const ThemeBackup& backup) {
    ThemeResult r;
    r.ok = true;
    for (const auto& [name, value] : backup.cursors)
        if (!api.writeCursor(name, value)) {
            r.ok = false;
            note(r, L"Curseur non rétabli : " + name);
        }
    if (!api.reloadCursors()) {
        r.ok = false;
        note(r, L"Windows n'a pas rechargé les curseurs");
    }
    const std::vector<std::wstring> screens = api.monitors();
    for (const auto& [id, path] : backup.wallpapers) {
        if (std::find(screens.begin(), screens.end(), id) == screens.end()) continue;   // écran débranché
        if (path.empty()) {   // diaporama, couleur unie… : pas de fichier à rendre
            note(r, L"Aucun fond d'écran d'origine connu pour " + id + L" : le fond actuel est gardé");
            continue;
        }
        if (!api.setWallpaper(id, path)) {
            r.ok = false;
            note(r, L"Fond d'écran non rétabli : " + id);
        }
    }
    return r;
}

} // namespace md
