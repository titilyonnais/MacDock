#include "crash_report.h"

#include <windows.h>
#include <dbghelp.h>

#include <cwchar>

#include "log.h"

namespace md {

namespace {

std::wstring g_dir, g_who;

using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                          PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);

MiniDumpWriteDumpFn g_write = nullptr;

struct Crash {
    EXCEPTION_POINTERS* info;
    DWORD thread;
};

void writeDump(const Crash& crash) {
    if (!g_write || g_dir.empty()) return;
    CreateDirectoryW(g_dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[96];
    swprintf_s(name, L"\\%s-%04u%02u%02u-%02u%02u%02u.dmp", g_who.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    const std::wstring path = g_dir + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION ex{crash.thread, crash.info, FALSE};
    const auto type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    if (g_write(GetCurrentProcess(), GetCurrentProcessId(), file, type, &ex, nullptr, nullptr))
        log::error(L"Plantage (%s) : vidage %s", g_who.c_str(), path.c_str());
    CloseHandle(file);
}

// Journal et vidage sur un fil à part : s'ils restent bloqués (verrou du tas, du chargeur ou du journal tenu par un
// fil gelé), le fil fautif termine quand même le processus au bout de 5 s.
DWORD WINAPI report(void* param) {
    const Crash& crash = *static_cast<const Crash*>(param);
    log::error(L"Plantage (%s) : exception 0x%08lX en %s (thread %lu)", g_who.c_str(),
               crash.info->ExceptionRecord->ExceptionCode, crashLocation(crash.info->ExceptionRecord->ExceptionAddress).c_str(),
               crash.thread);
    writeDump(crash);
    return 0;
}

LONG WINAPI filter(EXCEPTION_POINTERS* info) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) Sleep(INFINITE);   // second plantage simultané : le premier finit le rapport
    static Crash crash;
    crash = {info, GetCurrentThreadId()};
    if (HANDLE t = CreateThread(nullptr, 256 * 1024, report, &crash, 0, nullptr)) {
        WaitForSingleObject(t, 5000);
        CloseHandle(t);
    }
    // Fin immédiate : le lanceur voit le code d'exception et relance tout de suite.
    TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

std::wstring crashLocation(const void* address) {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        static_cast<LPCWSTR>(address), &module))
        return L"?";
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(module, path, MAX_PATH);
    const wchar_t* base = wcsrchr(path, L'\\');
    wchar_t out[MAX_PATH + 32];
    swprintf_s(out, L"%s+0x%llx", base ? base + 1 : path,
               static_cast<unsigned long long>(static_cast<const char*>(address) - reinterpret_cast<const char*>(module)));
    return out;
}

bool crashDumpReady() { return g_write != nullptr; }

void installCrashReport(const std::wstring& dumpDir, const std::wstring& who) {
    g_dir = dumpDir;
    g_who = who;
    if (!g_write)   // chargé ici (aucun lien pour le lanceur et les tests), depuis System32 seulement
        if (HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            g_write = reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    ULONG guarantee = 64 * 1024;   // pile de secours : le filtre tourne même après un débordement de pile
    SetThreadStackGuarantee(&guarantee);
    SetUnhandledExceptionFilter(filter);
}

} // namespace md
