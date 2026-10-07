// MacDockLauncher.exe : démarre MacDock.exe et MacMenuBar.exe (s'il est présent) et relance celui qui plante.
//   MacDockLauncher.exe              lance et surveille le Dock et la barre de menus
//   MacDockLauncher.exe --install    démarrage automatique à l'ouverture de session
//   MacDockLauncher.exe --uninstall  retire le démarrage automatique
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>
#include <vector>

#include "../core/log.h"
#include "supervisor.h"

namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"MacDock";

std::wstring exeDir() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    return s.substr(0, s.find_last_of(L'\\'));
}

std::wstring logDir() {
    PWSTR roaming = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) dir = roaming;
    CoTaskMemFree(roaming);
    dir += L"\\MacDock";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\logs";
}

int install() {
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring value = L"\"" + std::wstring(self) + L"\"";
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return 1;
    LONG r = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                            DWORD((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS ? 0 : 1;
}

int uninstall() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return 1;
    RegDeleteValueW(key, kRunValue);
    RegCloseKey(key);
    return 0;
}

void notifyGaveUp(const std::wstring& logs, const wchar_t* title, const wchar_t* text) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"MacDockLauncher", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    nid.hIcon = LoadIconW(nullptr, IDI_WARNING);
    wcscpy_s(nid.szTip, L"MacDock");
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    std::wstring body = std::wstring(text) + L" Journal : " + logs;
    wcsncpy_s(nid.szInfo, body.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIconW(NIM_ADD, &nid);
    Sleep(10000);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);
}

double nowSeconds() { return double(GetTickCount64()) / 1000.0; }

struct Child {
    md::ChildRole role;
    std::wstring exe;
    const wchar_t* name;           // journal
    const wchar_t* stoppedTitle;   // notification d'abandon
    const wchar_t* stoppedText;
    HANDLE process = nullptr;
};

bool startChild(Child& c) {
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + c.exe + L"\"";
    if (!CreateProcessW(c.exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        md::log::error(L"Impossible de lancer %s (%lu)", c.exe.c_str(), GetLastError());
        return false;
    }
    CloseHandle(pi.hThread);
    c.process = pi.hProcess;
    return true;
}

// « Quitter MacDock » : la barre de menus se ferme proprement (elle rend sa zone réservée), sinon on l'arrête.
void stopChild(Child& c) {
    if (!c.process) return;
    if (HWND bar = FindWindowW(L"MacMenuBarWindow", nullptr); bar && c.role == md::ChildRole::MenuBar)
        PostMessageW(bar, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(c.process, 3000) == WAIT_TIMEOUT) TerminateProcess(c.process, 0);
    CloseHandle(c.process);
    c.process = nullptr;
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    std::wstring args(cmdLine ? cmdLine : L"");
    if (args.find(L"--install") != std::wstring::npos) return install();
    if (args.find(L"--uninstall") != std::wstring::npos) return uninstall();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MacDockLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    std::wstring logs = logDir();
    md::log::init(logs);
    std::vector<Child> children{{md::ChildRole::Dock, exeDir() + L"\\MacDock.exe", L"Le Dock", L"MacDock s'est arrêté",
                                 L"Le Dock a planté plusieurs fois. La barre des tâches Windows a été rétablie."}};
    const std::wstring bar = exeDir() + L"\\MacMenuBar.exe";
    if (GetFileAttributesW(bar.c_str()) != INVALID_FILE_ATTRIBUTES)
        children.push_back({md::ChildRole::MenuBar, bar, L"La barre de menus", L"La barre de menus s'est arrêtée",
                            L"La barre de menus a planté plusieurs fois et n'est plus relancée."});
    md::Supervisor supervisor;
    for (auto& c : children)
        if (!startChild(c)) notifyGaveUp(logs, c.stoppedTitle, c.stoppedText);

    for (;;) {
        std::vector<HANDLE> handles;
        std::vector<std::size_t> index;
        for (std::size_t i = 0; i < children.size(); ++i)
            if (children[i].process) {
                handles.push_back(children[i].process);
                index.push_back(i);
            }
        if (handles.empty()) break;
        DWORD w = WaitForMultipleObjects(DWORD(handles.size()), handles.data(), FALSE, INFINITE);
        if (w < WAIT_OBJECT_0 || w >= WAIT_OBJECT_0 + handles.size()) break;
        Child& c = children[index[w - WAIT_OBJECT_0]];
        DWORD code = 1;
        GetExitCodeProcess(c.process, &code);
        CloseHandle(c.process);
        c.process = nullptr;
        switch (supervisor.onExit(c.role, code, nowSeconds())) {
            case md::ExitDecision::StopAll:
                md::log::info(L"Dock arrêté normalement : arrêt de la barre de menus");
                for (auto& other : children) stopChild(other);
                break;
            case md::ExitDecision::Forget:
                if (code == 0) {
                    md::log::info(L"%s : arrêt normal", c.name);
                } else {
                    md::log::error(L"%s : trop de plantages en 60 s, abandon", c.name);
                    notifyGaveUp(logs, c.stoppedTitle, c.stoppedText);
                }
                break;
            case md::ExitDecision::Relaunch:
                md::log::warn(L"%s : arrêt avec le code 0x%08lX, relance", c.name, code);
                Sleep(1000);
                if (!startChild(c)) notifyGaveUp(logs, c.stoppedTitle, c.stoppedText);
                break;
        }
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
