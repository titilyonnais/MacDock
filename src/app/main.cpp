// MacDock.exe : Dock façon macOS pour Windows.
//   MacDock.exe                  lance le Dock
//   MacDock.exe --quit           ferme le Dock en cours d'exécution
//   MacDock.exe --trace-windows  journalise le suivi des fenêtres (diagnostic)
//   MacDock.exe --snapshot f.png [--hover x]  rendu hors écran du Dock (x : curseur en points depuis le centre)
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

#include "../core/log.h"
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

    bool snapshot = args.find(L"--snapshot") != std::wstring::npos;
    HANDLE mutex = snapshot ? nullptr : CreateMutexW(nullptr, TRUE, L"Local\\MacDock");
    if (!snapshot && GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    int code = 0;
    {
        md::DockApp::Options options;
        options.trace = args.find(L"--trace-windows") != std::wstring::npos;
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        for (int i = 1; i + 1 < argc; ++i) {
            if (wcscmp(argv[i], L"--snapshot") == 0) options.snapshot = argv[i + 1];
            if (wcscmp(argv[i], L"--hover") == 0) options.hover = _wtof(argv[i + 1]);
        }
        LocalFree(argv);
        md::DockApp app;
        code = app.run(instance, options);
    }
    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return code;
}
