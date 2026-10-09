#include "force_quit_system.h"

#include <windows.h>
#include <tlhelp32.h>

#include "../tracker/app_identity.h"

namespace md {

namespace {

DWORD currentSession() {
    DWORD session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session);
    return session;
}

// Fenêtre figée que remplace une fenêtre fantôme (user32, présent depuis Windows Vista mais absent des en-têtes).
HWND hungWindowOf(HWND ghost) {
    using Fn = HWND(WINAPI*)(HWND);
    static const Fn fn =
        reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "HungWindowFromGhostWindow"));
    return fn ? fn(ghost) : nullptr;
}

void addWindow(std::vector<FqWindow>& out, HWND hwnd, bool hung) {
    const auto id = identifyWindow(hwnd);
    if (!id || id->pid == GetCurrentProcessId()) return;
    FqWindow w;
    w.appId = id->appId;
    w.name = id->displayName;
    w.launch = id->launch;
    w.aumid = id->aumid;
    w.exePath = id->exePath;
    w.pid = id->pid;
    w.hung = hung;
    out.push_back(std::move(w));
}

} // namespace

std::vector<FqWindow> forceQuitWindows() {
    std::vector<FqWindow> out;
    EnumWindows(
        [](HWND hwnd, LPARAM lp) -> BOOL {
            auto& windows = *reinterpret_cast<std::vector<FqWindow>*>(lp);
            wchar_t cls[16] = {};
            GetClassNameW(hwnd, cls, 16);
            if (wcscmp(cls, L"Ghost") == 0) {   // l'app figée qu'elle cache
                if (!IsWindowVisible(hwnd)) return TRUE;
                if (HWND hung = hungWindowOf(hwnd)) addWindow(windows, hung, true);
                return TRUE;
            }
            if (isDockEligibleWindow(hwnd)) addWindow(windows, hwnd, IsHungAppWindow(hwnd) != FALSE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&out));
    return out;
}

std::wstring forceQuitAppIdOf(HWND window) {
    if (!window) return {};
    HWND root = GetAncestor(window, GA_ROOTOWNER);
    if (!root) root = window;
    wchar_t cls[16] = {};
    GetClassNameW(root, cls, 16);
    if (wcscmp(cls, L"Ghost") == 0)
        if (HWND hung = hungWindowOf(root)) root = hung;
    DWORD pid = 0;
    GetWindowThreadProcessId(root, &pid);
    if (isExplorerExe(processImagePath(pid))) return sessionExplorer().appId;   // bureau, dossiers
    const auto id = identifyWindow(root);
    return id ? id->appId : std::wstring();
}

FqApp sessionExplorer() {
    FqApp e;
    DWORD shellPid = 0;
    if (HWND shell = GetShellWindow()) GetWindowThreadProcessId(shell, &shellPid);
    for (const FqProcess& p : sessionProcesses()) {   // le bureau, et les fenêtres ouvertes dans un processus à part
        if (!isExplorerExe(p.exePath)) continue;
        e.pids.push_back(p.pid);
        if (e.exePath.empty() || p.pid == shellPid) e.exePath = p.exePath;
    }
    if (e.pids.empty()) return {};
    e.appId = L"explorer";
    e.name = L"Explorateur de fichiers";
    e.launch = e.exePath;
    e.explorer = true;
    return e;
}

std::vector<FqProcess> sessionProcesses() {
    std::vector<FqProcess> out;
    const DWORD session = currentSession();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W pe{sizeof pe};
    for (BOOL more = Process32FirstW(snap, &pe); more; more = Process32NextW(snap, &pe)) {
        DWORD s = 0;
        if (pe.th32ProcessID <= 4 || !ProcessIdToSessionId(pe.th32ProcessID, &s) || s != session) continue;
        FqProcess p;
        p.pid = pe.th32ProcessID;
        p.exePath = processImagePath(pe.th32ProcessID);
        if (p.exePath.empty()) p.exePath = pe.szExeFile;   // processus protégé : son nom seulement
        p.aumid = processAppUserModelId(pe.th32ProcessID);
        out.push_back(std::move(p));
    }
    CloseHandle(snap);
    return out;
}

bool terminateProcesses(const std::vector<unsigned long>& pids, std::wstring* error) {
    bool ok = true;
    std::vector<HANDLE> ending;
    for (unsigned long pid : pids) {
        if (pid == 0 || pid == 4 || pid == GetCurrentProcessId()) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
        if (!h) {
            const DWORD e = GetLastError();
            if (e == ERROR_INVALID_PARAMETER) continue;   // déjà parti
            ok = false;
            if (error)
                *error = e == ERROR_ACCESS_DENIED ? L"Windows refuse de l'arrêter : elle a été lancée en administrateur."
                                                  : L"Windows refuse de l'arrêter (erreur " + std::to_wstring(e) + L").";
            continue;
        }
        if (!TerminateProcess(h, 1) && WaitForSingleObject(h, 0) != WAIT_OBJECT_0) {   // refus, sauf s'il finissait déjà
            const DWORD e = GetLastError();
            ok = false;
            if (error) *error = L"Windows refuse de l'arrêter (erreur " + std::to_wstring(e) + L").";
        }
        ending.push_back(h);
    }
    const ULONGLONG deadline = GetTickCount64() + 1000;
    for (HANDLE h : ending) {   // la liste relue juste après ne la montre plus
        const ULONGLONG now = GetTickCount64();
        WaitForSingleObject(h, now >= deadline ? 0 : DWORD(deadline - now));
        CloseHandle(h);
    }
    return ok;
}

} // namespace md
