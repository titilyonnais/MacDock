#include "theme_system.h"

#include <shobjidl.h>
#include <wrl/client.h>

#include "../calib/png_io.h"
#include "../config/config_store.h"
#include "../core/log.h"
#include "cursor_art.h"
#include "wallpaper_art.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {

constexpr wchar_t kCursorsKey[] = L"Control Panel\\Cursors";

ComPtr<IDesktopWallpaper> desktopWallpaper() {
    ComPtr<IDesktopWallpaper> w;
    CoCreateInstance(__uuidof(DesktopWallpaper), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&w));
    return w;
}

std::optional<RECT> monitorRect(IDesktopWallpaper* w, const std::wstring& id) {
    RECT rc{};
    if (!w || FAILED(w->GetMonitorRECT(id.c_str(), &rc)) || rc.right <= rc.left || rc.bottom <= rc.top) return std::nullopt;
    return rc;
}

bool fileExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

bool writeFileAtomic(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return false;
    const std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const BOOL ok = WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr) && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || written != bytes.size() ||
        !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

// Sauvegarde : absente → nullopt ; présente mais illisible → erreur (on ne la remplace pas par l'état actuel).
std::optional<ThemeBackup> loadBackup(bool& unreadable) {
    unreadable = false;
    const std::wstring path = themeBackupPath();
    if (!fileExists(path)) return std::nullopt;
    LoadResult file = loadJsonFile(path);
    std::optional<ThemeBackup> b = file.fromFile ? themeBackupFromJson(file.value) : std::nullopt;
    unreadable = !b;
    return b;
}

void logResult(const wchar_t* what, const ThemeResult& r) {
    if (r.ok) log::info(L"%s : fait%s%s", what, r.message.empty() ? L"" : L" — ", r.message.c_str());
    else log::warn(L"%s : échec — %s", what, r.message.c_str());
}

// Une ligne de curseurs (toutes les formes, première image) sur un fond uni.
void drawCursorRow(BgraImage& sheet, int top, int size, std::uint8_t back) {
    const int cell = 80;
    for (int y = top; y < top + cell; ++y)
        for (int x = 0; x < sheet.w; ++x) {
            std::uint8_t* p = &sheet.px[(std::size_t(y) * sheet.w + x) * 4];
            p[0] = p[1] = p[2] = back;
            p[3] = 255;
        }
    int i = 0;
    for (CursorKind k : kThemeCursors) {
        const std::vector<CursorFrame> frames = cursorFrames(k, size);
        const BgraImage& im = frames[0].image;
        const int ox = i * cell + (cell - size) / 2, oy = top + (cell - size) / 2;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const std::uint8_t* s = &im.px[(std::size_t(y) * size + x) * 4];
                std::uint8_t* d = &sheet.px[(std::size_t(oy + y) * sheet.w + ox + x) * 4];
                for (int c = 0; c < 3; ++c) d[c] = std::uint8_t((s[c] * s[3] + d[c] * (255 - s[3])) / 255);
            }
        ++i;
    }
}

} // namespace

ThemeApi realThemeApi() {
    ThemeApi a;
    a.readCursor = [](const std::wstring& name) -> std::optional<std::wstring> {
        const DWORD flags = RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND;   // %SystemRoot% gardé tel quel
        DWORD bytes = 0;
        if (RegGetValueW(HKEY_CURRENT_USER, kCursorsKey, name.c_str(), flags, nullptr, nullptr, &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
        bytes = DWORD(value.size() * sizeof(wchar_t));
        if (RegGetValueW(HKEY_CURRENT_USER, kCursorsKey, name.c_str(), flags, nullptr, value.data(), &bytes) != ERROR_SUCCESS)
            return std::nullopt;
        value.resize(wcsnlen(value.c_str(), value.size()));
        return value;
    };
    a.writeCursor = [](const std::wstring& name, const std::wstring& value) {
        return RegSetKeyValueW(HKEY_CURRENT_USER, kCursorsKey, name.c_str(), REG_EXPAND_SZ, value.c_str(),
                               DWORD((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    };
    a.reloadCursors = [] { return SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, SPIF_SENDCHANGE) != FALSE; };
    a.monitors = [] {
        std::vector<std::wstring> ids;
        ComPtr<IDesktopWallpaper> w = desktopWallpaper();
        UINT count = 0;
        if (!w || FAILED(w->GetMonitorDevicePathCount(&count))) return ids;
        for (UINT i = 0; i < count; ++i) {
            LPWSTR id = nullptr;
            if (FAILED(w->GetMonitorDevicePathAt(i, &id)) || !id) continue;
            if (monitorRect(w.Get(), id)) ids.push_back(id);   // écran branché seulement
            CoTaskMemFree(id);
        }
        return ids;
    };
    a.getWallpaper = [](const std::wstring& id) {
        std::wstring path;
        ComPtr<IDesktopWallpaper> w = desktopWallpaper();
        LPWSTR p = nullptr;
        if (w && SUCCEEDED(w->GetWallpaper(id.c_str(), &p)) && p) path = p;
        if (p) CoTaskMemFree(p);
        return path;
    };
    a.setWallpaper = [](const std::wstring& id, const std::wstring& path) {
        ComPtr<IDesktopWallpaper> w = desktopWallpaper();
        return w && SUCCEEDED(w->SetWallpaper(id.c_str(), path.c_str()));
    };
    a.writeFile = writeFileAtomic;
    a.darkMode = [] {
        DWORD value = 1, size = sizeof(value);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"SystemUsesLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
        return value == 0;
    };
    a.monitorSize = [](const std::wstring& id) {
        ComPtr<IDesktopWallpaper> w = desktopWallpaper();
        const std::optional<RECT> rc = monitorRect(w.Get(), id);
        return rc ? SIZE{rc->right - rc->left, rc->bottom - rc->top} : SIZE{0, 0};
    };
    a.saveBackup = [](const ThemeBackup& b) { return saveJsonFileAtomic(themeBackupPath(), themeBackupToJson(b)); };
    return a;
}

std::wstring themeBackupPath() { return appDataDir() + L"\\theme-backup.json"; }

bool themeBackupExists() { return fileExists(themeBackupPath()); }

ThemeResult applyMacTheme() {
    bool unreadable = false;
    std::optional<ThemeBackup> backup = loadBackup(unreadable);
    ThemeResult r;
    if (unreadable) {
        r.message = L"La sauvegarde du thème Windows est illisible (" + themeBackupPath() + L") : rien n'a été changé";
    } else {
        const std::wstring dir = appDataDir() + L"\\theme";
        CreateDirectoryW(dir.c_str(), nullptr);
        ThemeApi api = realThemeApi();
        r = applyTheme(api, dir, backup);
    }
    logResult(L"Thème macOS appliqué", r);
    return r;
}

ThemeResult restoreWindowsTheme() {
    bool unreadable = false;
    std::optional<ThemeBackup> backup = loadBackup(unreadable);
    ThemeResult r;
    if (!backup) {
        r.message = unreadable ? L"La sauvegarde du thème Windows est illisible (" + themeBackupPath() + L")"
                               : std::wstring(L"Aucune sauvegarde du thème Windows : rien à rétablir");
    } else {
        ThemeApi api = realThemeApi();
        r = restoreTheme(api, *backup);
        if (r.ok) DeleteFileW(themeBackupPath().c_str());   // en cas d'échec partiel, on garde de quoi réessayer
    }
    logResult(L"Thème Windows rétabli", r);
    return r;
}

bool writeThemeSnapshot(const std::wstring& dir) {
    BgraImage sheet{10 * 80, 4 * 80, std::vector<std::uint8_t>(std::size_t(10 * 80) * 4 * 80 * 4, 0)};
    drawCursorRow(sheet, 0, 64, 236);
    drawCursorRow(sheet, 80, 32, 236);
    drawCursorRow(sheet, 160, 64, 40);
    drawCursorRow(sheet, 240, 32, 40);
    bool ok = writePng(dir + L"\\cursors.png", sheet.px.data(), UINT(sheet.w), UINT(sheet.h));
    for (bool dark : {false, true}) {
        const BgraImage wall = tahoeWallpaper(1920, 1080, dark);
        ok = writePng(dir + (dark ? L"\\wallpaper-dark.png" : L"\\wallpaper-light.png"), wall.px.data(), 1920, 1080) && ok;
    }
    return ok;
}

} // namespace md
