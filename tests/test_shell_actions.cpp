// Actions système : logique pure (sans toucher au registre de l'utilisateur).
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
