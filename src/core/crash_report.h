// Rapport de plantage : journal (module + décalage, symbolisable avec le .pdb de la même version), vidage mémoire
// dans dumpDir, puis fin immédiate du processus (le lanceur relance aussitôt, sans attendre le rapport d'erreurs
// de Windows qui figeait le Dock une trentaine de secondes).
#pragma once
#include <string>

namespace md {

// who : nom court dans le journal et le nom du vidage (« dock », « barre »).
void installCrashReport(const std::wstring& dumpDir, const std::wstring& who);
// « MacDock.exe+0x1a2b3 » ; « ? » si l'adresse n'appartient à aucun module.
std::wstring crashLocation(const void* address);
// MiniDumpWriteDump résolu à l'installation (jamais de chargement de DLL pendant un plantage).
bool crashDumpReady();

} // namespace md
