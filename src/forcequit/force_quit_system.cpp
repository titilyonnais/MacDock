#include "force_quit_system.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>

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

// Le processus ouvert est-il bien le programme relu ? Poignée tenue : son identité ne change plus. Chemin complet
// comparé, ou nom de fichier seul si l'instantané n'avait que lui (processus protégé).
bool sameProgram(HANDLE process, const std::wstring& expected) {
    wchar_t path[MAX_PATH * 2];
    DWORD size = DWORD(std::size(path));
    if (!QueryFullProcessImageNameW(process, 0, path, &size)) return false;
    const std::wstring actual(path, size);
    if (expected.find_first_of(L"\\/") != std::wstring::npos) return _wcsicmp(actual.c_str(), expected.c_str()) == 0;
    const std::size_t slash = actual.find_last_of(L"\\/");
    return _wcsicmp(actual.c_str() + (slash == std::wstring::npos ? 0 : slash + 1), expected.c_str()) == 0;
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
            wchar_t cls[32] = {};
            GetClassNameW(hwnd, cls, 32);
            if (wcscmp(cls, L"Ghost") == 0) {   // l'app figée qu'elle cache
                if (!IsWindowVisible(hwnd)) return TRUE;
                if (HWND hung = hungWindowOf(hwnd)) addWindow(windows, hung, true);
                return TRUE;
            }
            // App du Store : le cadre d'ApplicationFrameHost répond toujours ; c'est sa CoreWindow qui se fige.
            if (!isDockEligibleWindow(hwnd)) return TRUE;
            HWND core = wcscmp(cls, L"ApplicationFrameWindow") == 0 ? coreWindowOf(hwnd) : nullptr;
            addWindow(windows, hwnd, IsHungAppWindow(hwnd) || (core && IsHungAppWindow(core)));
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
        if (pe.th32ProcessID <= 4) continue;
        FqProcess p;
        p.pid = pe.th32ProcessID;
        if (!ProcessIdToSessionId(pe.th32ProcessID, &s)) {   // protégé (csrss, dwm, winlogon…) : gardé par son nom
            p.exePath = pe.szExeFile;
            out.push_back(std::move(p));
            continue;
        }
        if (s != session) continue;
        p.exePath = processImagePath(pe.th32ProcessID);
        if (p.exePath.empty()) p.exePath = pe.szExeFile;
        p.aumid = processAppUserModelId(pe.th32ProcessID);
        out.push_back(std::move(p));
    }
    CloseHandle(snap);
    return out;
}

bool terminateProcesses(const std::vector<FqProcess>& targets, std::wstring* error) {
    bool ok = true;
    std::vector<HANDLE> ending;
    for (const FqProcess& t : targets) {
        const DWORD pid = t.pid;
        if (pid == 0 || pid == 4 || pid == GetCurrentProcessId()) continue;
        HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (h && !sameProgram(h, t.exePath)) {   // PID repris par un autre programme : l'app visée est partie
            CloseHandle(h);
            continue;
        }
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

bool forceQuitNow(const std::wstring& appId, std::wstring* error,
                  const std::function<bool(const std::vector<FqProcess>&)>& allow) {
    const std::vector<FqApp> apps = forceQuitApps(forceQuitWindows(), FqApp{});
    const auto app = std::find_if(apps.begin(), apps.end(), [&](const FqApp& a) { return a.appId == appId; });
    if (app == apps.end()) return true;   // partie entre-temps : rien à arrêter, rien à dire
    const std::vector<FqProcess> processes = sessionProcesses();
    std::vector<FqProcess> targets;
    for (unsigned long pid : forceQuitTargets(*app, processes, GetCurrentProcessId()))
        for (const FqProcess& p : processes)
            if (p.pid == pid) targets.push_back(p);
    if (targets.empty()) {
        if (error) *error = L"Ses processus sont introuvables.";
        return false;
    }
    if (allow && !allow(targets)) {
        if (error) *error = L"Refusé.";
        return false;
    }
    return terminateProcesses(targets, error);
}

} // namespace md
