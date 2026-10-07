// Diagnostic image par image (variable d'environnement MACDOCK_DIAG=1) : traces détaillées, et les surfaces animées
// (génie GPU, sprites) restent visibles aux enregistreurs d'écran au lieu d'être exclues des captures.
#pragma once
#include <windows.h>

namespace md {

inline bool diagnosticCapture() {
    static const bool on = GetEnvironmentVariableW(L"MACDOCK_DIAG", nullptr, 0) > 0;
    return on;
}

} // namespace md
