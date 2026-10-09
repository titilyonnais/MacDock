#include "explorer_restart.h"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#pragma comment(lib, "shell32.lib")

namespace md {

void restartExplorer(const std::wstring& windowsDir) {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe{sizeof pe};
        for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
            DWORD s = 0;
            if (_wcsicmp(pe.szExeFile, L"explorer.exe") || !ProcessIdToSessionId(pe.th32ProcessID, &s) || s != session) continue;
            if (HANDLE p = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID)) {
                TerminateProcess(p, 1);
                CloseHandle(p);
            }
        }
        CloseHandle(snap);
    }
    for (int i = 0; i < 20; ++i) {   // Winlogon relance le bureau en une à deux secondes
        Sleep(250);
        if (FindWindowW(L"Shell_TrayWnd", nullptr)) return;
    }
    SHELLEXECUTEINFOW sei{sizeof sei};
    const std::wstring exe = windowsDir + L"\\explorer.exe";
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpFile = exe.c_str();
    sei.nShow = SW_SHOWDEFAULT;
    ShellExecuteExW(&sei);
}

} // namespace md
