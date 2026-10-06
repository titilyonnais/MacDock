#include "log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#include "strings.h"

namespace md::log {
namespace {

std::mutex g_mutex;
std::wstring g_dir;
constexpr long long kMaxBytes = 1024 * 1024;

std::wstring path(int index) {
    return index == 0 ? g_dir + L"\\log.txt" : g_dir + L"\\log." + std::to_wstring(index) + L".txt";
}

void rotateIfNeeded() {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path(0).c_str(), GetFileExInfoStandard, &data)) return;
    long long size = (long long(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    if (size < kMaxBytes) return;
    DeleteFileW(path(2).c_str());
    MoveFileW(path(1).c_str(), path(2).c_str());
    MoveFileW(path(0).c_str(), path(1).c_str());
}

void write(const wchar_t* level, const wchar_t* fmt, va_list args) {
    wchar_t message[2048];
    _vsnwprintf_s(message, _TRUNCATE, fmt, args);
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t line[2200];
    swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] %s\r\n", t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level, message);
    OutputDebugStringW(line);

    std::lock_guard lock(g_mutex);
    if (g_dir.empty()) return;
    rotateIfNeeded();
    HANDLE f = CreateFileW(path(0).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    std::string utf8 = toUtf8(line);
    DWORD written = 0;
    WriteFile(f, utf8.data(), DWORD(utf8.size()), &written, nullptr);
    CloseHandle(f);
}

} // namespace

void init(const std::wstring& dir) {
    std::lock_guard lock(g_mutex);
    g_dir = dir;
    CreateDirectoryW(dir.c_str(), nullptr);
}

void info(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); write(L"INFO", fmt, a); va_end(a); }
void warn(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); write(L"WARN", fmt, a); va_end(a); }
void error(const wchar_t* fmt, ...) { va_list a; va_start(a, fmt); write(L"ERROR", fmt, a); va_end(a); }

} // namespace md::log
