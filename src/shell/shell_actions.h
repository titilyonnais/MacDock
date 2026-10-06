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
void revealInExplorer(const std::wstring& path);

// « Ouvrir à la connexion » : valeur « MacDock: <nom> » de HKCU\…\CurrentVersion\Run.
bool isOpenAtLogin(const std::wstring& exePath);
bool runCommandLaunches(const std::wstring& command, const std::wstring& exePath);   // casse ignorée
bool setOpenAtLogin(const std::wstring& exePath, const std::wstring& name, bool on);

bool recycleBinHasItems();
void emptyRecycleBin(HWND owner);   // avec la confirmation de l'Explorateur

} // namespace md
