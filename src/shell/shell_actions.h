// Actions système déclenchées par le Dock.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace md {

bool launch(const std::wstring& target);            // exe, .lnk, dossier, shell:AppsFolder\AUMID
void activateApp(const std::vector<HWND>& windows); // restaure les réduites, met tout au premier plan
void restoreWindow(HWND hwnd);
void minimizeAll(const std::vector<HWND>& windows);
void openRecycleBin();
void openFolder(const std::wstring& path);
void openStartMenu();
std::wstring downloadsFolder();
bool forceForeground(HWND hwnd);

} // namespace md
