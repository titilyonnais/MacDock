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

void writeDump(EXCEPTION_POINTERS* info) {
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");   // chargé à la demande : aucun lien pour le lanceur et les tests
    auto write = dbghelp ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump")) : nullptr;
    if (!write || g_dir.empty()) return;
    CreateDirectoryW(g_dir.c_str(), nullptr);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t name[96];
    swprintf_s(name, L"\\%s-%04u%02u%02u-%02u%02u%02u.dmp", g_who.c_str(), t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
               t.wSecond);
    const std::wstring path = g_dir + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION ex{GetCurrentThreadId(), info, FALSE};
    const auto type = MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    if (write(GetCurrentProcess(), GetCurrentProcessId(), file, type, &ex, nullptr, nullptr))
        log::error(L"Plantage (%s) : vidage %s", g_who.c_str(), path.c_str());
    CloseHandle(file);
}

LONG WINAPI filter(EXCEPTION_POINTERS* info) {
    static volatile LONG once = 0;
    if (InterlockedExchange(&once, 1)) TerminateProcess(GetCurrentProcess(), info->ExceptionRecord->ExceptionCode);
    log::error(L"Plantage (%s) : exception 0x%08lX en %s (thread %lu)", g_who.c_str(), info->ExceptionRecord->ExceptionCode,
               crashLocation(info->ExceptionRecord->ExceptionAddress).c_str(), GetCurrentThreadId());
    writeDump(info);
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

void installCrashReport(const std::wstring& dumpDir, const std::wstring& who) {
    g_dir = dumpDir;
    g_who = who;
    SetUnhandledExceptionFilter(filter);
}

} // namespace md
