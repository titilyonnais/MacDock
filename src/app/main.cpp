// MacDock.exe : Dock façon macOS pour Windows.
//   MacDock.exe                  lance le Dock
//   MacDock.exe --quit           ferme le Dock (et la barre de menus) en cours d'exécution
//   MacDock.exe --trace-windows  journalise le suivi des fenêtres (diagnostic)
//   MacDock.exe --snapshot f.png [--hover x]  rendu hors écran du Dock (x : curseur en points depuis le centre)
//               [--theme light|dark] [--wallpaper fond.png] [--reference ref.png --diff diff.png]   calibration (cf. reference/README.md)
//   MacDock.exe --capture-test f.png   capture réelle du bas de l'écran (diagnostic du verre)
//   MacDock.exe --menu-test            menu en verre de démonstration (diagnostic)
//   MacDock.exe --genie-snapshot f.png [--effect genie|scale] [--edge bottom|left|right]
//               planche hors écran de l'effet de réduction (aucune fenêtre, aucun réglage lu ni écrit)
//   MacDock.exe --theme apply|restore   applique le thème macOS (curseurs, fond d'écran) ou rétablit celui de Windows,
//               sans lancer le Dock (sauvegarde : %APPDATA%\MacDock\theme-backup.json ; code de sortie 1 si échec)
//   MacDock.exe --theme-snapshot dossier   planche des curseurs et fonds d'écran, sans rien appliquer
//   MacDock.exe --apps-snapshot f.png [--query texte] [--page n] [--theme light|dark]
//               écran Apps hors écran avec les vraies apps (dossier Apps lu, rien lancé, aucune fenêtre)
//   MacDock.exe --spotlight-snapshot f.png [--query texte] [--theme light|dark]
//               panneau Spotlight hors écran (vraies apps, documents de l'index en lecture seule, aucune fenêtre)
//   MacDock.exe --mission-snapshot f.png [--count n] [--hover i] [--theme light|dark]
//               Mission Control hors écran avec des fenêtres factices (aucune fenêtre réelle touchée)
//   MacDock.exe --switcher-snapshot f.png [--count n] [--select i] [--theme light|dark]
//               sélecteur d'apps hors écran avec des apps factices (aucun raccourci enregistré)
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>

#include <algorithm>
#include <string>
#include <vector>

#include "../anim/genie_preview.h"
#include "window_capture.h"
#include "../apps/apps_folder.h"
#include "../apps/apps_window.h"
#include "../calib/png_io.h"
#include "../mission/mission_view.h"
#include "../spotlight/file_search.h"
#include "../switcher/switcher_window.h"
#include "../spotlight/spotlight_window.h"
#include "../config/config_store.h"
#include "../core/log.h"
#include "cli_args.h"
#include "../theme/theme_system.h"
#include "capture_test.h"
#include "menu_test.h"
#include "dock_window.h"

namespace {

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    md::log::error(L"Plantage : exception 0x%08lX à l'adresse %p", info->ExceptionRecord->ExceptionCode,
                   info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;   // le lanceur verra un code de sortie non nul et relancera
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    std::wstring args(cmdLine ? cmdLine : L"");
    if (args.find(L"--quit") != std::wstring::npos) {
        if (HWND dock = FindWindowW(L"MacDockWindow", nullptr)) PostMessageW(dock, WM_CLOSE, 0, 0);
        if (HWND bar = FindWindowW(L"MacMenuBarWindow", nullptr)) PostMessageW(bar, WM_CLOSE, 0, 0);   // la barre aussi
        return 0;
    }

    {   // une option de diagnostic sans valeur ne doit jamais démarrer un vrai Dock
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::wstring> list(argv, argv + (argv ? argc : 0));
        if (argv) LocalFree(argv);
        if (const std::wstring bad = md::diagnosticMissingValue(list); !bad.empty()) {
            md::log::init(md::appDataDir() + L"\\logs");
            md::log::error(L"Option sans valeur ou mal écrite : %s ; le Dock ne démarre pas", bad.c_str());
            return 2;
        }
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetUnhandledExceptionFilter(crashFilter);

    // Thème : --theme apply|restore (--theme light|dark reste l'option de --snapshot).
    std::wstring themeAction, themeSnapshot;
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--theme") == 0 && (wcscmp(argv[i + 1], L"apply") == 0 || wcscmp(argv[i + 1], L"restore") == 0))
                themeAction = argv[i + 1];
            if (wcscmp(argv[i], L"--theme-snapshot") == 0) themeSnapshot = argv[i + 1];
        }
        LocalFree(argv);
    }
    bool snapshot = args.find(L"--snapshot") != std::wstring::npos || args.find(L"--capture-test") != std::wstring::npos ||
                    args.find(L"--menu-test") != std::wstring::npos || args.find(L"--genie-snapshot") != std::wstring::npos ||
                    !themeAction.empty() || !themeSnapshot.empty() ||
                    args.find(L"--apps-snapshot") != std::wstring::npos ||
                    args.find(L"--spotlight-snapshot") != std::wstring::npos ||
                    args.find(L"--mission-snapshot") != std::wstring::npos ||
                    args.find(L"--switcher-snapshot") != std::wstring::npos ||
                    args.find(L"--genie-capture-probe") != std::wstring::npos;
    HANDLE mutex = snapshot ? nullptr : CreateMutexW(nullptr, TRUE, L"Local\\MacDock");
    if (!snapshot && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    OleInitialize(nullptr);   // COM en STA + glisser-déposer OLE (RegisterDragDrop)
    int code = 0;
    {
        md::DockApp::Options options;
        options.trace = args.find(L"--trace-windows") != std::wstring::npos;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--snapshot") == 0) options.snapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--hover") == 0) options.hover = _wtof(argv[i + 1]);
            if (wcscmp(argv[i], L"--wallpaper") == 0) options.wallpaper = argv[i + 1];
            if (wcscmp(argv[i], L"--reference") == 0) options.reference = argv[i + 1];
            if (wcscmp(argv[i], L"--diff") == 0) options.diff = argv[i + 1];
            if (wcscmp(argv[i], L"--theme") == 0) options.dark = wcscmp(argv[i + 1], L"dark") == 0;
        }
        std::wstring captureProbe;
        std::wstring captureTest, genieSnapshot, appsSnapshot, appsQuery, spotSnapshot, missionSnapshot, switcherSnapshot;
        int countArg = -1, missionHover = -1, switcherSelect = 1;   // countArg < 0 : nombre par défaut
        int appsPage = 0;
        md::MinimizeEffect effect = md::MinimizeEffect::Genie;
        md::DockPosition edge = md::DockPosition::Bottom;
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--capture-test") == 0) captureTest = argv[i + 1];
            if (wcscmp(argv[i], L"--genie-snapshot") == 0) genieSnapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--genie-capture-probe") == 0) captureProbe = argv[i + 1];
            if (wcscmp(argv[i], L"--apps-snapshot") == 0) appsSnapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--spotlight-snapshot") == 0) spotSnapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--mission-snapshot") == 0) missionSnapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--switcher-snapshot") == 0) switcherSnapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--select") == 0) switcherSelect = std::max(0, _wtoi(argv[i + 1]));
            if (wcscmp(argv[i], L"--count") == 0) countArg = std::clamp(_wtoi(argv[i + 1]), 0, 60);
            if (wcscmp(argv[i], L"--hover") == 0) missionHover = _wtoi(argv[i + 1]);
            if (wcscmp(argv[i], L"--query") == 0) appsQuery = argv[i + 1];
            if (wcscmp(argv[i], L"--page") == 0) appsPage = _wtoi(argv[i + 1]);
            if (wcscmp(argv[i], L"--effect") == 0 && wcscmp(argv[i + 1], L"scale") == 0) effect = md::MinimizeEffect::Scale;
            if (wcscmp(argv[i], L"--edge") == 0)
                edge = wcscmp(argv[i + 1], L"left") == 0    ? md::DockPosition::Left
                       : wcscmp(argv[i + 1], L"right") == 0 ? md::DockPosition::Right
                                                            : md::DockPosition::Bottom;
        }
        LocalFree(argv);
        if (!captureProbe.empty()) {
            md::log::init(md::appDataDir() + L"\\logs");
            code = md::genieCaptureProbe(GetModuleHandleW(nullptr), captureProbe) ? 0 : 1;
        } else if (!switcherSnapshot.empty()) {
            static const wchar_t* kNames[] = {L"Explorateur", L"Navigateur", L"Terminal", L"Éditeur de code", L"Musique",
                                              L"Photos", L"Courrier", L"Calendrier", L"Notes", L"Calculatrice"};
            std::vector<std::wstring> names;
            const int count = countArg < 0 ? 4 : countArg;
            for (int i = 0; i < count; ++i) names.push_back(kNames[i % std::size(kNames)]);
            const md::BgraImage im = md::switcherSnapshot(names, std::size_t(switcherSelect), options.dark.value_or(false), 1920, 1080);
            code = md::writePng(switcherSnapshot, im.px.data(), UINT(im.w), UINT(im.h)) ? 0 : 1;
        } else if (!missionSnapshot.empty()) {
            std::vector<md::MissionRect> wins;   // fenêtres factices, toujours les mêmes
            for (int i = 0, n = countArg < 0 ? 6 : countArg; i < n; ++i)
                wins.push_back({double(i * 211 % 1200), double(i * 131 % 600), 640.0 + i * 173 % 700, 420.0 + i * 97 % 420});
            const md::BgraImage im = md::missionSnapshot(wins, options.dark.value_or(false), 1920, 1080, missionHover);
            code = md::writePng(missionSnapshot, im.px.data(), UINT(im.w), UINT(im.h)) ? 0 : 1;
        } else if (!spotSnapshot.empty()) {
            wchar_t profile[MAX_PATH] = {};
            GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
            const md::BgraImage im = md::spotlightSnapshot(appsQuery, md::catalogFrom(md::readAppsFolder()),
                                                           md::wantsFileSearch(appsQuery) ? md::searchFiles(appsQuery, profile, 8)
                                                                                         : std::vector<md::SpotItem>{},
                                                           options.dark.value_or(false), 1920, 1080);
            code = md::writePng(spotSnapshot, im.px.data(), UINT(im.w), UINT(im.h)) ? 0 : 1;
        } else if (!appsSnapshot.empty()) {
            md::AppsIconStyle style;
            style.dark = options.dark.value_or(false);
            const md::BgraImage im = md::appsSnapshot(md::catalogFrom(md::readAppsFolder()), appsQuery, appsPage,
                                                      style.dark, 1920, 1080, true, style);
            code = md::writePng(appsSnapshot, im.px.data(), UINT(im.w), UINT(im.h)) ? 0 : 1;
        } else if (!themeSnapshot.empty()) {
            code = md::writeThemeSnapshot(themeSnapshot) ? 0 : 1;
        } else if (!themeAction.empty()) {
            md::log::init(md::appDataDir() + L"\\logs");
            code = (themeAction == L"apply" ? md::applyMacTheme() : md::restoreWindowsTheme()).ok ? 0 : 1;
        } else if (!genieSnapshot.empty()) {
            const md::BgraImage sheet = md::genieSheet(effect, edge);
            code = md::writePng(genieSnapshot, sheet.px.data(), UINT(sheet.w), UINT(sheet.h)) ? 0 : 1;
        } else if (args.find(L"--menu-test") != std::wstring::npos) {
            md::log::init(md::appDataDir() + L"\\logs");
            code = md::runMenuTest(instance);
        } else if (!captureTest.empty()) {
            md::log::init(md::appDataDir() + L"\\logs");
            code = md::runCaptureTest(captureTest);
        } else {
            md::DockApp app;
            code = app.run(instance, options);
        }
    }
    OleUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
