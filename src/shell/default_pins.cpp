#include "default_pins.h"

#include <windows.h>
#include <shlobj.h>

#include <algorithm>

#include "../core/log.h"
#include "../core/strings.h"
#include "../model/app_model.h"
#include "../tracker/app_identity.h"
#include "shell_actions.h"

namespace md {
namespace {

std::wstring windowsDir() {
    wchar_t buf[MAX_PATH];
    GetWindowsDirectoryW(buf, MAX_PATH);
    return buf;
}

// Raccourcis épinglés à la barre des tâches, dans l'ordre de la barre (ordre de création).
std::vector<std::wstring> taskbarPinnedShortcuts() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) dir = roaming;
    CoTaskMemFree(roaming);
    dir += L"\\Microsoft\\Internet Explorer\\Quick Launch\\User Pinned\\TaskBar";

    struct Entry { std::wstring path; FILETIME created; };
    std::vector<Entry> entries;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.lnk").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        entries.push_back({dir + L"\\" + fd.cFileName, fd.ftCreationTime});
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(entries.begin(), entries.end(),
              [](auto& a, auto& b) { return CompareFileTime(&a.created, &b.created) < 0; });
    std::vector<std::wstring> out;
    for (auto& e : entries) out.push_back(e.path);
    return out;
}

} // namespace

std::vector<PinnedEntry> defaultPins() {
    std::vector<PinnedEntry> pins;
    std::wstring explorer = windowsDir() + L"\\explorer.exe";
    pins.push_back({PinKind::App, toLower(explorer), explorer, L"Explorateur de fichiers", explorer});
    pins.push_back({PinKind::AppsButton, L"", L"", L"Apps", L""});

    for (auto& lnk : taskbarPinnedShortcuts()) {
        auto id = identifyLaunchTarget(lnk);
        if (!id) continue;
        // L'Explorateur est déjà la première épingle (raccourci « Explorateur de fichiers » de la barre).
        if (toLower(id->exePath) == toLower(explorer) || id->aumid == L"Microsoft.Windows.Explorer") continue;
        bool dup = std::any_of(pins.begin(), pins.end(), [&](auto& p) { return p.appId == id->appId; });
        if (dup) continue;
        pins.push_back({PinKind::App, id->appId, lnk, id->displayName, id->exePath});
        log::info(L"Épingle importée : %s (%s)", id->displayName.c_str(), id->appId.c_str());
    }

    std::wstring downloads = downloadsFolder();
    if (!downloads.empty()) pins.push_back({PinKind::Stack, L"", downloads, L"Téléchargements", L""});
    return pins;
}

} // namespace md
