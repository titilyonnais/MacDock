// Fenêtre d'app de test avec une barre de menus Win32, sur son propre fil (comme une autre app), hors écran.
#pragma once
#include <windows.h>

#include <thread>

namespace test {

// Fenêtre d'app sur son propre fil (comme une autre app) : son menu Fichier gagne une entrée à chaque
// WM_INITMENUPOPUP ; busyMs > 0 : le fil dort avant de traiter ses messages (app occupée).
struct MenuApp {
    MenuApp(const MenuApp&) = delete;
    MenuApp& operator=(const MenuApp&) = delete;
    HWND hwnd = nullptr;
    HMENU bar = nullptr, file = nullptr;
    std::thread thread;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITMENUPOPUP) AppendMenuW(reinterpret_cast<HMENU>(wp), MF_STRING, 150, L"&Ajoutée");
        return DefWindowProcW(h, msg, wp, lp);
    }
    explicit MenuApp(DWORD busyMs = 0) {
        thread = std::thread([this, busyMs] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"MacDockTestMenuApp";
            RegisterClassW(&wc);
            bar = CreateMenu();
            file = CreatePopupMenu();
            AppendMenuW(file, MF_STRING, 101, L"&Nouveau");
            AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Fichier");
            HMENU edit = CreatePopupMenu();
            AppendMenuW(edit, MF_STRING, 201, L"&Copier\tCtrl+C");
            AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(edit), L"&Édition");
            hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, -3000, -3000, 200, 200,
                                   nullptr, bar, wc.hInstance, nullptr);
            SetEvent(ready);
            if (busyMs) Sleep(busyMs);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
            DestroyWindow(hwnd);
        });
        WaitForSingleObject(ready, 5000);
    }
    ~MenuApp() {
        PostThreadMessageW(GetThreadId(thread.native_handle()), WM_QUIT, 0, 0);
        thread.join();
        CloseHandle(ready);
    }
};

} // namespace test
