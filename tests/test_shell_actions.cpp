// Actions système : logique pure (sans toucher au registre de l'utilisateur).
#include <windows.h>
#include <objbase.h>

#include "minitest.h"
#include "../src/shell/shell_actions.h"

TEST_CASE(run_command_matches_exe) {
    const std::wstring exe = L"C:\\Program Files\\App\\app.exe";
    CHECK(md::runCommandLaunches(L"\"C:\\Program Files\\App\\app.exe\"", exe));
    CHECK(md::runCommandLaunches(L"\"c:\\program files\\app\\APP.exe\" --minimized", exe));
    CHECK(md::runCommandLaunches(L"C:\\Tools\\t.exe /background", L"C:\\Tools\\t.exe"));
    CHECK(md::runCommandLaunches(L"C:\\Tools\\t.exe", L"c:\\tools\\T.EXE"));
    // Un autre exécutable du même dossier, ou un préfixe, ne correspond pas.
    CHECK(!md::runCommandLaunches(L"\"C:\\Program Files\\App\\app.exe.bak\"", exe));
    CHECK(!md::runCommandLaunches(L"C:\\Tools\\t.exe2", L"C:\\Tools\\t.exe"));
    CHECK(!md::runCommandLaunches(L"", exe));
    CHECK(!md::runCommandLaunches(L"\"C:\\x.exe\"", L""));
}

TEST_CASE(open_with_quotes_each_path) {
    CHECK(md::quoteArguments({L"C:/a b/c.txt", L"D:/x.png"}) == L"\"C:/a b/c.txt\" \"D:/x.png\"");
    CHECK(md::quoteArguments({}).empty());
}

TEST_CASE(startup_shortcut_name_is_stable) {
    CHECK(md::startupShortcutName(L"Calculatrice") == L"MacDock - Calculatrice.lnk");
    CHECK(md::startupShortcutName(L"Calculatrice") == md::startupShortcutName(L"Calculatrice"));
    CHECK(md::startupShortcutName(L"") == L"MacDock - App.lnk");
}

TEST_CASE(startup_shortcut_name_strips_forbidden) {
    CHECK(md::startupShortcutName(L"A/B\\C:D*E?F\"G<H>I|J") == L"MacDock - A_B_C_D_E_F_G_H_I_J.lnk");
    CHECK(md::startupShortcutName(L"Fin. ") == L"MacDock - Fin.lnk");   // ni point ni espace final
    CHECK(md::startupShortcutName(L"Bip\x01") == L"MacDock - Bip_.lnk");
}

TEST_CASE(packaged_login_shortcut_roundtrip) {
    // Dans un dossier temporaire : le dossier Démarrage de l'utilisateur n'est jamais touché par les tests.
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"macdock-startup-test-" + std::to_wstring(GetCurrentProcessId());
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring aumid = L"Microsoft.WindowsCalculator_8wekyb3d8bbwe!App";
    CHECK(!md::isPackagedOpenAtLogin(L"Calculatrice", dir));
    CHECK(md::setPackagedOpenAtLogin(aumid, L"Calculatrice", true, dir));
    CHECK(md::isPackagedOpenAtLogin(L"Calculatrice", dir));
    CHECK(GetFileAttributesW((dir + L"\\MacDock - Calculatrice.lnk").c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(md::setPackagedOpenAtLogin(aumid, L"Calculatrice", false, dir));
    CHECK(!md::isPackagedOpenAtLogin(L"Calculatrice", dir));
    RemoveDirectoryW(dir.c_str());
    CoUninitialize();
}
