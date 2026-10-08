// Actions système déclenchées par le Dock.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

namespace md {

bool launch(const std::wstring& target);            // exe, .lnk, dossier, shell:AppsFolder\AUMID
// Même lancement sur un fil à part (COM STA) : ShellExecuteEx peut bloquer plusieurs secondes (app froide, disque
// en veille, invite de sécurité) et ne doit jamais figer le Dock. Les échecs sont journalisés par launcher.
void launchAsync(std::wstring target, bool (*launcher)(const std::wstring&) = launch);
void activateApp(const std::vector<HWND>& windows); // restaure les réduites, met tout au premier plan
void restoreWindow(HWND hwnd);
// App Réglages de MacDock (MacDockSettings.exe, à côté de l'exécutable courant), sur la section `pane` (--pane) ou celle
// par défaut ; false si l'app manque (l'appelant ouvre alors le fichier JSON).
bool openMacDockSettings(const std::wstring& pane);
// Réduction demandée par MacDock : le Dock en est prévenu avant (MacDockWillMinimize), pour animer lui-même la
// fenêtre sans que Windows joue aussi la sienne.
void minimizeWindow(HWND hwnd, int command = SW_MINIMIZE);
void minimizeAll(const std::vector<HWND>& windows);
void openRecycleBin();
void openFolder(const std::wstring& path);
void openStartMenu();
std::wstring downloadsFolder();
bool forceForeground(HWND hwnd);
// Frappe qui débloque le verrou de premier plan : Alt enfoncé, une touche non attribuée, Alt relâché. Jamais Alt
// seul : relâché seul, il ouvrirait la barre de menus de l'app au premier plan (fenêtre « figée » jusqu'au clic suivant).
std::vector<INPUT> foregroundUnlockKeys();
void revealInExplorer(const std::wstring& path);

// « Ouvrir à la connexion » : valeur « MacDock: <nom> » de HKCU\…\CurrentVersion\Run.
bool isOpenAtLogin(const std::wstring& exePath);
bool runCommandLaunches(const std::wstring& command, const std::wstring& exePath);   // casse ignorée
bool setOpenAtLogin(const std::wstring& exePath, const std::wstring& name, bool on);
// Apps empaquetées (sans exe lançable) : raccourci « MacDock - <nom>.lnk » vers shell:AppsFolder\<AUMID> dans le
// dossier Démarrage (folder vide), ou dans folder (tests).
std::wstring startupShortcutName(const std::wstring& displayName);   // caractères interdits remplacés
bool isPackagedOpenAtLogin(const std::wstring& displayName, const std::wstring& folder = {});
bool setPackagedOpenAtLogin(const std::wstring& aumid, const std::wstring& displayName, bool on,
                            const std::wstring& folder = {});

// Dépôt de fichiers. openWith : AUMID non vide → activation de l'app empaquetée, sinon exe.
bool openWith(const std::wstring& exePath, const std::wstring& aumid, const std::vector<std::wstring>& paths);
bool recycle(const std::vector<std::wstring>& paths, HWND owner);                 // progression standard
bool moveInto(const std::vector<std::wstring>& paths, const std::wstring& folder, HWND owner);
std::wstring quoteArguments(const std::vector<std::wstring>& paths);             // "a" "b"

bool recycleBinHasItems();
// Avec la confirmation de l'Explorateur ; quiet : sans le son de Windows (le nôtre le remplace). true : vidée.
bool emptyRecycleBin(HWND owner, bool quiet = false);

} // namespace md
