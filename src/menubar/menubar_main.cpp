// MacMenuBar.exe : barre de menus façon macOS pour Windows.
//   MacMenuBar.exe                lance la barre
//   MacMenuBar.exe --quit         ferme la barre en cours d'exécution
//   MacMenuBar.exe --trace        journalise l'app active, la couleur du texte, les menus (diagnostic)
//   MacMenuBar.exe --lights-snapshot f.png   planche des feux tricolores (aucune fenêtre, aucun réglage)
//   MacMenuBar.exe --snapshot f.png [--wallpaper fond.png] [--app Nom] [--theme light|dark] [--open k]
//                                 rendu hors écran de la barre (k : titre dont le menu est ouvert)
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "../calib/png_io.h"
#include "../core/log.h"
#include "menubar_window.h"
#include "traffic_lights.h"

namespace {

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    md::log::error(L"Plantage de la barre : exception 0x%08lX à l'adresse %p", info->ExceptionRecord->ExceptionCode,
                   info->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_CONTINUE_SEARCH;   // le lanceur verra un code de sortie non nul et relancera
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR cmdLine, int) {
    std::wstring args(cmdLine ? cmdLine : L"");
    if (args.find(L"--quit") != std::wstring::npos) {
        if (HWND bar = FindWindowW(L"MacMenuBarWindow", nullptr)) PostMessageW(bar, WM_CLOSE, 0, 0);
        return 0;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetUnhandledExceptionFilter(crashFilter);

    md::MenuBarApp::Options options;
    options.trace = args.find(L"--trace") != std::wstring::npos;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring lightsSnapshot;
    for (int i = 1; i + 1 < argc; ++i) {
        if (wcscmp(argv[i], L"--lights-snapshot") == 0) lightsSnapshot = argv[i + 1];
        if (wcscmp(argv[i], L"--snapshot") == 0) options.snapshot = argv[i + 1];
        if (wcscmp(argv[i], L"--wallpaper") == 0) options.wallpaper = argv[i + 1];
        if (wcscmp(argv[i], L"--app") == 0) options.app = argv[i + 1];
        if (wcscmp(argv[i], L"--theme") == 0) options.dark = wcscmp(argv[i + 1], L"dark") == 0;
        if (wcscmp(argv[i], L"--open") == 0) options.open = _wtoi(argv[i + 1]);
    }
    LocalFree(argv);
    if (!lightsSnapshot.empty()) {   // aucune fenêtre, aucun réglage lu ni écrit
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // WIC
        UINT w = 0, h = 0;
        const auto sheet = md::lightsSheet(w, h);
        const bool ok = md::writePng(lightsSnapshot, sheet.data(), w, h);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    const bool snapshot = !options.snapshot.empty();
    HANDLE mutex = snapshot ? nullptr : CreateMutexW(nullptr, TRUE, L"Local\\MacMenuBar");
    if (!snapshot && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // Shell (Explorateur, propriétés), WIC
    int code = 0;
    {
        md::MenuBarApp app;
        code = app.run(instance, options);
    }
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
