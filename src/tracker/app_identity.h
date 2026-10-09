// Identité d'application d'une fenêtre (AUMID, exécutable, nom affiché).
#pragma once
#include <windows.h>

#include <optional>
#include <string>

#include "../model/app_model.h"

namespace md {

// Fenêtre qui mérite une place dans le Dock (équivalent des fenêtres d'Alt+Tab).
bool isDockEligibleWindow(HWND hwnd);

std::optional<AppIdentity> identifyWindow(HWND hwnd);

// Identité d'une cible épinglée (.lnk, exe) : AUMID du raccourci s'il existe, sinon exe cible.
std::optional<AppIdentity> identifyLaunchTarget(const std::wstring& path);

// Nom lisible d'un exécutable (FileDescription), sinon nom de fichier sans extension.
std::wstring exeDisplayName(const std::wstring& exePath);

// Fenêtre de l'app dans le cadre d'une app du Store (ApplicationFrameWindow d'ApplicationFrameHost) : sa
// CoreWindow, d'un autre processus ; nulle si l'app est réduite (CoreWindow détachée).
HWND coreWindowOf(HWND frame);

// Chemin de l'exécutable d'un processus ; AUMID d'un processus empaqueté (app du Store…). Vide si inconnu.
std::wstring processImagePath(DWORD pid);
std::wstring processAppUserModelId(DWORD pid);

} // namespace md
