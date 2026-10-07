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
