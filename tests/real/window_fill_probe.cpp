// Essai réel du plan 72 (fenêtres de macOS 26) sur une fenêtre d'essai à soi, sans MacDock : rendue à sa taille sans
// animation depuis l'état agrandi, Remplir reconnu (bascule vers la taille d'avant), Remplir sans marges. Cadres
// visibles (DWMWA_EXTENDED_FRAME_BOUNDS) comparés au pixel.
// Compilation (Developer Command Prompt), depuis la racine du dépôt :
//   cl /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX tests\real\window_fill_probe.cpp src\shell\window_tile.cpp
//      /link user32.lib dwmapi.lib
#include <windows.h>
#include <dwmapi.h>

#include <cstdio>
#include <thread>

#include "../../src/shell/window_tile.h"

static HWND g_win;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY && h == g_win) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}

static int failures = 0;
static void check(bool ok, const char* what) {
    failures += ok ? 0 : 1;
    std::printf("%-48s %s\n", what, ok ? "OK" : "ÉCHEC");
}
static void same(const RECT& want, const char* what) {
    const RECT got = md::windowFrame(g_win);
    const bool ok = EqualRect(&want, &got) != FALSE;
    failures += ok ? 0 : 1;
    std::printf("%-48s voulu (%ld,%ld)-(%ld,%ld) obtenu (%ld,%ld)-(%ld,%ld) %s\n", what, want.left, want.top, want.right,
                want.bottom, got.left, got.top, got.right, got.bottom, ok ? "OK" : "ÉCART");
}

static void scenario() {
    Sleep(400);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(g_win, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT original = md::windowFrame(g_win);
    // Agrandie puis rendue sans animation : cadre d'avant.
    ShowWindow(g_win, SW_MAXIMIZE);
    Sleep(300);
    check(md::restoreWithoutAnimation(g_win) && !IsZoomed(g_win), "rendue depuis l'état agrandi");
    same(original, "  à son cadre d'avant");
    check(md::restoreWithoutAnimation(g_win), "déjà rendue : rien à faire");
    // Remplir, reconnu comme tel, puis la taille d'avant.
    check(md::tileWindow(g_win, md::TileAction::Fill), "Remplir");
    check(md::tiledBy(g_win, md::TileAction::Fill, md::windowFrame(g_win)), "  reconnue remplie");
    check(!md::tiledBy(g_win, md::TileAction::Left, md::windowFrame(g_win)), "  pas « à gauche »");
    check(!md::tiledBy(g_win, md::TileAction::Fill, original), "  autre cadre : pas remplie");
    check(md::tileWindow(g_win, md::TileAction::Previous), "taille d'avant");
    same(original, "  cadre d'avant");
    check(!md::tiledBy(g_win, md::TileAction::Fill, md::windowFrame(g_win)), "  plus remplie");
    // Remplir depuis l'agrandissement : toute la zone entre la barre et le Dock, sans marges (ta remarque du 12 octobre).
    check(md::tileWindow(g_win, md::TileAction::Fill, 0), "Remplir sans marges");
    same(mi.rcWork, "  zone de travail entière");
    check(md::tiledBy(g_win, md::TileAction::Fill, md::windowFrame(g_win)), "  reconnue remplie");
    check(md::tileWindow(g_win, md::TileAction::Previous), "taille d'avant");
    same(original, "  cadre d'avant");
    std::printf("%s\n", failures ? "ÉCHEC" : "tout est exact");
    PostMessageW(g_win, WM_CLOSE, 0, 0);
}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacDockFillProbe";
    RegisterClassW(&wc);
    g_win = CreateWindowExW(0, wc.lpszClassName, L"Essai Remplir et plein écran (MacDock)", WS_OVERLAPPEDWINDOW, 300, 250, 900,
                            600, nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(g_win, SW_SHOWNOACTIVATE);
    std::thread t(scenario);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    t.join();
    return failures ? 1 : 0;
}
