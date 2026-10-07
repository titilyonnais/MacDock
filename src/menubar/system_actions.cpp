// Actions système réelles de la barre (veille, verrouillage, session, redémarrage, extinction). Compilé dans
// MacMenuBar.exe seulement : les tests utilisent de fausses actions.
#include <windows.h>
#include <powrprof.h>

#include "../core/log.h"
#include "bar_actions.h"

namespace md {
namespace {

// Redémarrer et éteindre exigent le privilège d'arrêt du processus.
bool enableShutdownPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(token, FALSE, &tp, 0, nullptr, nullptr) && GetLastError() == ERROR_SUCCESS;
    CloseHandle(token);
    return ok;
}

void exitWindows(UINT flags, const wchar_t* what) {
    if ((flags & (EWX_REBOOT | EWX_POWEROFF)) && !enableShutdownPrivilege())
        log::warn(L"Barre : privilège d'arrêt refusé (%s)", what);
    if (!ExitWindowsEx(flags, SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_FLAG_PLANNED))
        log::error(L"Barre : %s impossible (%lu)", what, GetLastError());
}

} // namespace

SystemActions realSystemActions() {
    SystemActions s;
    s.sleep = [] {
        // SetSuspendState exige aussi le privilège d'arrêt. Sur une machine en veille moderne (S0), il n'a pas d'effet.
        if (!enableShutdownPrivilege()) log::warn(L"Barre : privilège d'arrêt refusé (veille)");
        if (!SetSuspendState(FALSE, FALSE, FALSE)) log::error(L"Barre : mise en veille impossible (%lu)", GetLastError());
    };
    s.lock = [] { LockWorkStation(); };
    s.signOut = [] { exitWindows(EWX_LOGOFF, L"fermeture de session"); };
    s.restart = [] { exitWindows(EWX_REBOOT, L"redémarrage"); };
    s.shutdown = [] { exitWindows(EWX_POWEROFF, L"extinction"); };
    s.confirm = [](const std::wstring& question) {
        return MessageBoxW(nullptr, question.c_str(), L"MacDock", MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
    };
    return s;
}

} // namespace md
