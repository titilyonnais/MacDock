// Essai réel du plan 50 (« Déplacer et redimensionner ») sur des fenêtres d'essai à soi, sans MacDock : tileWindow
// range la fenêtre, et son cadre visible (DWMWA_EXTENDED_FRAME_BOUNDS) est comparé au cadre voulu (tileRect).
//   Gauche, En bas à droite, Revenir (au tout premier cadre), Remplir depuis l'état agrandi, Centrer ;
//   une fenêtre de largeur minimale 2200 px (WM_GETMINMAXINFO) rangée à droite : recalée contre le bord droit.
// Compilation (Developer Command Prompt), depuis la racine du dépôt :
//   cl /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX tests\real\window_tile_probe.cpp src\shell\window_tile.cpp
//      /link user32.lib dwmapi.lib
#include <windows.h>
#include <dwmapi.h>

#include <cstdio>
#include <thread>

#include "../../src/shell/window_tile.h"

static HWND g_win, g_wide;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_GETMINMAXINFO && h == g_wide && g_wide) {   // une app à largeur minimale (Electron à 150 %)
        reinterpret_cast<MINMAXINFO*>(l)->ptMinTrackSize.x = 2200;
        return 0;
    }
    if (m == WM_DESTROY && h == g_win) PostQuitMessage(0);
    return DefWindowProcW(h, m, w, l);
}

static RECT frame(HWND h) {
    RECT r{};
    DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r);
    return r;
}

static int failures = 0;
static void show(HWND h, const char* what, const RECT& want) {
    const RECT got = frame(h);
    const bool ok = EqualRect(&want, &got) != FALSE;
    failures += ok ? 0 : 1;
    std::printf("%-26s voulu (%ld,%ld)-(%ld,%ld) obtenu (%ld,%ld)-(%ld,%ld) %s\n", what, want.left, want.top, want.right,
                want.bottom, got.left, got.top, got.right, got.bottom, ok ? "OK" : "ÉCART");
}

static void scenario() {
    Sleep(400);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(g_win, MONITOR_DEFAULTTONEAREST), &mi);
    UINT dpi = GetDpiForWindow(g_win);
    const int margin = MulDiv(8, int(dpi ? dpi : 96), 96);
    const RECT original = frame(g_win);
    md::tileWindow(g_win, md::TileAction::Left);
    show(g_win, "Gauche", md::tileRect(md::TileAction::Left, mi.rcWork, original, margin));
    md::tileWindow(g_win, md::TileAction::BottomRight);
    show(g_win, "En bas à droite", md::tileRect(md::TileAction::BottomRight, mi.rcWork, original, margin));
    md::tileWindow(g_win, md::TileAction::Previous);
    show(g_win, "Revenir (au premier)", original);
    ShowWindow(g_win, SW_MAXIMIZE);
    Sleep(300);
    md::tileWindow(g_win, md::TileAction::Fill);
    std::printf("agrandie, après Remplir : %s\n", IsZoomed(g_win) ? "ENCORE AGRANDIE" : "restaurée");
    const RECT filled = md::tileRect(md::TileAction::Fill, mi.rcWork, original, margin);
    show(g_win, "Remplir", filled);
    md::tileWindow(g_win, md::TileAction::Center);
    show(g_win, "Centrer (taille gardée)", md::tileRect(md::TileAction::Center, mi.rcWork, filled, margin));
    // Largeur minimale plus grande que la moitié : recalée contre le bord droit.
    const RECT right = md::tileRect(md::TileAction::Right, mi.rcWork, frame(g_wide), margin);
    md::tileWindow(g_wide, md::TileAction::Right);
    const RECT wideGot = frame(g_wide);
    show(g_wide, "Droite, largeur minimale", md::anchorTile(md::TileAction::Right, right, wideGot));
    std::printf("bord droit : %ld (voulu %ld) ; largeur %ld\n", wideGot.right, right.right, wideGot.right - wideGot.left);
    std::printf("%s\n", failures ? "ÉCHEC" : "tout est exact");
    DestroyWindow(g_wide);
    PostMessageW(g_win, WM_CLOSE, 0, 0);
}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacDockTileProbe";
    RegisterClassW(&wc);
    g_win = CreateWindowExW(0, wc.lpszClassName, L"Essai rangement (MacDock)", WS_OVERLAPPEDWINDOW, 300, 250, 900, 600, nullptr,
                            nullptr, wc.hInstance, nullptr);
    g_wide = CreateWindowExW(0, wc.lpszClassName, L"Essai largeur minimale (MacDock)", WS_OVERLAPPEDWINDOW, 200, 200, 2300, 600,
                             nullptr, nullptr, wc.hInstance, nullptr);
    ShowWindow(g_win, SW_SHOWNOACTIVATE);
    ShowWindow(g_wide, SW_SHOWNOACTIVATE);
    std::thread t(scenario);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    t.join();
    return failures ? 1 : 0;
}
