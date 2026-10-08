// MacDockSettings.exe : l'app Réglages de MacDock. Une seule instance ; une seconde ouverture (`--pane dock`, par
// exemple depuis le Dock) amène la fenêtre existante sur la section demandée.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "../config/config_store.h"
#include "../core/log.h"
#include "../core/strings.h"
#include "settings_window.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    md::PaneId pane = md::PaneId::Dock;
    bool paneGiven = false;
    std::wstring dataDir;   // --data <dossier> : un autre dossier de réglages (essais)
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--data") dataDir = argv[i + 1];
        for (int i = 1; i + 1 < argc; ++i)
            if (std::wstring(argv[i]) == L"--pane")
                if (auto p = md::paneFromKey(md::toUtf8(argv[i + 1]))) {
                    pane = *p;
                    paneGiven = true;
                }
        LocalFree(argv);
    }
    // Sans --pane (menu ), une seconde ouverture garde la section affichée.
    std::size_t paneIndex = std::size_t(-1);
    for (std::size_t i = 0; paneGiven && i < md::paneList().size(); ++i)
        if (md::paneList()[i].id == pane) paneIndex = i;

    HANDLE single = CreateMutexW(nullptr, TRUE, L"MacDockSettings.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {   // déjà ouverte : elle passe devant, sur la bonne section
        HWND other = nullptr;
        for (int i = 0; i < 30 && !other; ++i) {   // la première instance peut être en train de créer sa fenêtre
            other = FindWindowW(md::SettingsWindow::kClass, nullptr);
            if (!other) Sleep(100);
        }
        if (other) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            SendMessageTimeoutW(other, md::SettingsWindow::paneMessage(), paneIndex, 0, SMTO_ABORTIFHUNG, 2000, nullptr);
        }
        if (single) CloseHandle(single);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring dir = dataDir.empty() ? md::appDataDir() : dataDir;
    md::log::init(dir + L"\\logs\\settings");
    int code = 1;
    {
        md::SettingsWindow window;
        if (window.create(instance, dir, pane, !dataDir.empty())) code = window.run();
        else md::log::error(L"Réglages : fenêtre impossible (%lu)", GetLastError());
    }
    CoUninitialize();
    if (single) CloseHandle(single);
    return code;
}
