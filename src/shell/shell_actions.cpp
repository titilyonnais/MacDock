#include "shell_actions.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>

#include "../core/log.h"

namespace md {

bool launch(const std::wstring& target) {
    SHELLEXECUTEINFOW sei{sizeof sei};
    sei.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = target.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (ShellExecuteExW(&sei)) return true;
    log::warn(L"Lancement impossible de %s (%lu)", target.c_str(), GetLastError());
    return false;
}

bool forceForeground(HWND hwnd) {
    if (SetForegroundWindow(hwnd)) return true;
    // Le Dock (WS_EX_NOACTIVATE) vient de recevoir le clic, mais Windows peut encore refuser :
    // une frappe Alt synthétique débloque le verrou de premier plan.
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_MENU;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
    return SetForegroundWindow(hwnd) != FALSE;
}

void activateApp(const std::vector<HWND>& windows) {
    if (windows.empty()) return;
    // Ordre Z actuel (du haut vers le bas) pour préserver l'empilement relatif.
    std::vector<HWND> ordered;
    for (HWND h = GetTopWindow(nullptr); h; h = GetWindow(h, GW_HWNDNEXT))
        if (std::find(windows.begin(), windows.end(), h) != windows.end()) ordered.push_back(h);
    for (HWND h : windows)
        if (std::find(ordered.begin(), ordered.end(), h) == ordered.end()) ordered.push_back(h);

    for (HWND h : ordered)
        if (IsIconic(h)) ShowWindowAsync(h, SW_RESTORE);
    // Du bas vers le haut : chaque fenêtre passe au-dessus de la précédente.
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it)
        SetWindowPos(*it, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
    forceForeground(ordered.front());
}

void restoreWindow(HWND hwnd) {
    if (IsIconic(hwnd)) ShowWindowAsync(hwnd, SW_RESTORE);
    forceForeground(hwnd);
}

void minimizeAll(const std::vector<HWND>& windows) {
    for (HWND h : windows) ShowWindowAsync(h, SW_MINIMIZE);
}

void openRecycleBin() { launch(L"shell:RecycleBinFolder"); }

void openFolder(const std::wstring& path) { launch(path); }

void openStartMenu() {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = VK_LWIN;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

std::wstring downloadsFolder() {
    PWSTR path = nullptr;
    std::wstring out;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path))) out = path;
    CoTaskMemFree(path);
    return out;
}

} // namespace md
