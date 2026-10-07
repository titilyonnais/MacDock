// MacDock.exe : Dock façon macOS pour Windows.
//   MacDock.exe                  lance le Dock
//   MacDock.exe --quit           ferme le Dock en cours d'exécution
//   MacDock.exe --trace-windows  journalise le suivi des fenêtres (diagnostic)
//   MacDock.exe --snapshot f.png [--hover x]  rendu hors écran du Dock (x : curseur en points depuis le centre)
//               [--theme light|dark] [--wallpaper fond.png] [--reference ref.png --diff diff.png]   calibration (cf. reference/README.md)
//   MacDock.exe --capture-test f.png   capture réelle du bas de l'écran (diagnostic du verre)
//   MacDock.exe --menu-test            menu en verre de démonstration (diagnostic)
#include <windows.h>
#include <objbase.h>
#include <ole2.h>
#include <shellapi.h>

#include <string>

#include "../config/config_store.h"
#include "../core/log.h"
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
        return 0;
    }

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetUnhandledExceptionFilter(crashFilter);

    bool snapshot = args.find(L"--snapshot") != std::wstring::npos || args.find(L"--capture-test") != std::wstring::npos ||
                    args.find(L"--menu-test") != std::wstring::npos;
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
        std::wstring captureTest;
        for (int i = 1; i + 1 < argc; ++i)
            if (wcscmp(argv[i], L"--capture-test") == 0) captureTest = argv[i + 1];
        LocalFree(argv);
        if (args.find(L"--menu-test") != std::wstring::npos) {
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
