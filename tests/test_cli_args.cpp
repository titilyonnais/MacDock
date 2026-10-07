// Ligne de commande de MacDock.exe : une option de diagnostic sans valeur ne lance jamais le Dock.
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/app/cli_args.h"

TEST_CASE(cli_diagnostic_without_value) {   // relecture finale, important 3
    using V = std::vector<std::wstring>;
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe"}).empty());
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--apps-snapshot", L"f.png"}).empty());
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--apps-snapshot"}) == L"--apps-snapshot");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--apps-snapshot", L"--theme", L"dark"}) == L"--apps-snapshot");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--apps-snapshot=f.png"}) == L"--apps-snapshot=f.png");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--snapshot", L"f.png", L"--theme"}) == L"--theme");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--genie-snapshot"}) == L"--genie-snapshot");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--spotlight-snapshot"}) == L"--spotlight-snapshot");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--theme-snapshot"}) == L"--theme-snapshot");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--capture-test"}) == L"--capture-test");
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--trace-windows"}).empty());   // option sans valeur
    CHECK(md::diagnosticMissingValue(V{L"MacDock.exe", L"--quit"}).empty());
}
