// Rapport de plantage : où (module et décalage, symbolisable avec le .pdb de la même version).
#include "minitest.h"
#include <windows.h>

#include <string>

#include "../src/core/crash_report.h"

namespace {
int probeFunction(int x) { return x * 3 + 1; }
} // namespace

TEST_CASE(crash_location_names_module_and_offset) {
    const std::wstring at = md::crashLocation(reinterpret_cast<const void*>(&probeFunction));
    CHECK(at.rfind(L"tests.exe+0x", 0) == 0);   // module de l'adresse, décalage en hexadécimal
    CHECK(md::crashLocation(nullptr) == L"?");
    CHECK(probeFunction(1) == 4);
}

TEST_CASE(crash_report_resolves_dump_writer_up_front) {
    // Charger dbghelp pendant le plantage peut bloquer pour toujours (verrou du chargeur tenu par un autre fil) : le
    // processus ne se terminerait jamais et le lanceur ne relancerait rien. Tout est résolu à l'installation.
    LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(nullptr);
    md::installCrashReport(L"", L"test");
    CHECK(md::crashDumpReady());
    SetUnhandledExceptionFilter(previous);
}
