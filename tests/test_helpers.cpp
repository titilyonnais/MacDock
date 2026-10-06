#include "test_helpers.h"

#include <windows.h>

namespace md {

std::wstring testTempDir() {
    static int counter = 0;
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring base = std::wstring(tmp) + L"macdock-tests";
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring dir = base + L"\\" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(++counter);
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

void testWriteFile(const std::wstring& path, const std::string& bytes) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(f, bytes.data(), DWORD(bytes.size()), &written, nullptr);
    CloseHandle(f);
}

bool testFileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

} // namespace md
