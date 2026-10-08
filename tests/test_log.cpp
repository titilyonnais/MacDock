// Journal : dossier créé en entier, même quand ses parents n'existent pas (dossier d'essai neuf de l'app Réglages).
#include <windows.h>

#include <string>

#include "minitest.h"
#include "../src/core/log.h"

TEST_CASE(log_creates_nested_directories) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring root = std::wstring(tmp) + L"macdock-log-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                              std::to_wstring(GetTickCount64());
    const std::wstring dir = root + L"\\logs\\settings";
    md::log::init(dir);
    md::log::info(L"essai du journal");
    CHECK(GetFileAttributesW((dir + L"\\log.txt").c_str()) != INVALID_FILE_ATTRIBUTES);
    md::log::init(L"");   // les tests suivants n'écrivent plus nulle part
    DeleteFileW((dir + L"\\log.txt").c_str());
    RemoveDirectoryW(dir.c_str());
    RemoveDirectoryW((root + L"\\logs").c_str());
    RemoveDirectoryW(root.c_str());
}
