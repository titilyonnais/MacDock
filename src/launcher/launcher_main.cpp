// MacDockLauncher.exe : démarre MacDock.exe et le relance s'il plante.
//   MacDockLauncher.exe              lance et surveille le Dock
//   MacDockLauncher.exe --install    démarrage automatique à l'ouverture de session
//   MacDockLauncher.exe --uninstall  retire le démarrage automatique
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <string>

#include "../core/log.h"
#include "crash_policy.h"

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

void notifyGaveUp(const std::wstring& logs) {
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"MacDockLauncher", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_TIP | NIF_INFO;
    nid.hIcon = LoadIconW(nullptr, IDI_WARNING);
    wcscpy_s(nid.szTip, L"MacDock");
    wcscpy_s(nid.szInfoTitle, L"MacDock s'est arrêté");
    std::wstring text = L"Le Dock a planté plusieurs fois. La barre des tâches Windows a été rétablie. Journal : " + logs;
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    nid.dwInfoFlags = NIIF_WARNING;
    Shell_NotifyIconW(NIM_ADD, &nid);
    Sleep(10000);
    Shell_NotifyIconW(NIM_DELETE, &nid);
    DestroyWindow(hwnd);
}

double nowSeconds() { return double(GetTickCount64()) / 1000.0; }

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    std::wstring args(cmdLine ? cmdLine : L"");
    if (args.find(L"--install") != std::wstring::npos) return install();
    if (args.find(L"--uninstall") != std::wstring::npos) return uninstall();

    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\MacDockLauncher");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    std::wstring logs = logDir();
    md::log::init(logs);
    std::wstring dock = exeDir() + L"\\MacDock.exe";
    md::CrashPolicy policy;

    for (;;) {
        STARTUPINFOW si{sizeof si};
        PROCESS_INFORMATION pi{};
        std::wstring cmd = L"\"" + dock + L"\"";
        if (!CreateProcessW(dock.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
            md::log::error(L"Impossible de lancer %s (%lu)", dock.c_str(), GetLastError());
            notifyGaveUp(logs);
            break;
        }
        CloseHandle(pi.hThread);
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 1;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hProcess);
        if (code == 0) {
            md::log::info(L"Dock arrêté normalement");
            break;
        }
        md::log::warn(L"Le Dock s'est arrêté avec le code 0x%08lX", code);
        if (!policy.onCrash(nowSeconds())) {
            md::log::error(L"Trop de plantages en 60 s : abandon");
            notifyGaveUp(logs);
            break;
        }
        Sleep(1000);
    }
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return 0;
}
