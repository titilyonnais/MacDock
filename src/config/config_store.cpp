#include "config_store.h"

#include <windows.h>
#include <shlobj.h>

#include "../core/log.h"
#include "../core/strings.h"

namespace md {
namespace {

bool readAll(const std::wstring& path, std::string& out, bool& exists) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        exists = GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND;
        return false;
    }
    exists = true;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    if (size.QuadPart > 16 * 1024 * 1024) { CloseHandle(f); return false; }
    out.resize(size_t(size.QuadPart));
    DWORD read = 0;
    BOOL ok = out.empty() || ReadFile(f, out.data(), DWORD(out.size()), &read, nullptr);
    CloseHandle(f);
    return ok && read == out.size();
}

} // namespace

std::wstring appDataDir() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) dir = roaming;
    CoTaskMemFree(roaming);
    dir += L"\\MacDock";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

LoadResult loadJsonFile(const std::wstring& path) {
    LoadResult r;
    r.value = json::Object{};
    std::string text;
    bool exists = false;
    if (!readAll(path, text, exists)) {
        if (exists) {
            r.unreadable = true;
            log::warn(L"Lecture impossible : %s", path.c_str());
        }
        return r;
    }
    // BOM UTF-8 éventuel (fichier édité avec le Bloc-notes).
    if (text.size() >= 3 && text.compare(0, 3, "\xEF\xBB\xBF") == 0) text.erase(0, 3);
    std::string error;
    auto parsed = json::parse(text, &error);
    if (!parsed || !parsed->isObject()) {
        r.wasInvalid = true;
        CopyFileW(path.c_str(), (path + L".bak").c_str(), FALSE);
        log::warn(L"JSON invalide dans %s : %s — valeurs par défaut utilisées, copie en .bak",
                  path.c_str(), fromUtf8(error).c_str());
        return r;
    }
    r.value = std::move(*parsed);
    r.fromFile = true;
    return r;
}

bool saveJsonFileAtomic(const std::wstring& path, const json::Value& v) {
    std::string text = json::serialize(v, true);
    std::wstring tmp = path + L".tmp";
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(f, text.data(), DWORD(text.size()), &written, nullptr) && FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || written != text.size()) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace md

namespace md {
bool shouldImportDefaultPins(const LoadResult& file, const Settings& parsed) {
    if (file.wasInvalid || file.unreadable) return false;
    return !parsed.pinnedInitialized;
}

namespace {
bool same(const json::Value* a, const json::Value& b) { return a && json::serialize(*a, false) == json::serialize(b, false); }
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

json::Value dockSettingsToWrite(const LoadResult& file, const json::Value& lastSaved, const json::Value& now) {
    if (!file.fromFile || file.wasInvalid || file.unreadable) return now;
    return mergeChanged(file.value, lastSaved, now);
}
} // namespace md

