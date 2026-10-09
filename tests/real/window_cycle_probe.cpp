// Essai réel de « Fenêtre suivante de l'app » (plan 59, ⌘` de macOS), sans jamais toucher aux apps de l'utilisateur.
// La sonde ouvre trois fenêtres à elle (une app pour le Dock, qui la reconnaît à son exécutable), prend le premier
// plan, puis envoie de vraies touches que le Dock lancé en diagnostic (MACDOCK_DIAG=1) accepte :
//   1. Alt+` trois fois : 1 → 2 → 3 → 1 (ordre d'ouverture, en boucle) ;
//   2. Alt+Maj+` : 1 → 3 ;
//   3. la deuxième réduite : 3 → 1 → 3 (elle est sautée) ;
//   4. Alt+` tenu (trois répétitions) : une seule fenêtre de plus ;
//   5. la touche n'arrive jamais aux fenêtres, et Alt relâché n'ouvre pas leur menu.
// Avant chaque touche, une fenêtre de la sonde doit avoir le premier plan ; sinon l'essai s'arrête sans rien envoyer.
// Construction : ./build.ps1 -Target windowcycleprobe, puis build\Debug\window_cycle_probe.exe
#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <vector>

namespace {

int g_failures = 0;
bool g_stop = false;    // le premier plan a quitté la sonde : plus aucune touche
HWND g_windows[3] = {};
int g_keyReached = 0;   // la touche sous Échap arrivée à une fenêtre de la sonde (elle devait être avalée)
int g_keyMenu = 0;      // Alt relâché a demandé le menu d'une fenêtre (la touche neutre du Dock devait l'empêcher)

void check(bool ok, const wchar_t* what) {
    wprintf(L"%s  %s\n", ok ? L"OK    " : L"ÉCHEC ", what);
    if (!ok) ++g_failures;
}

void pump(DWORD ms) {
    const ULONGLONG end = GetTickCount64() + ms;
    MSG m;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    }
}

LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (((l >> 16) & 0xFF) == 0x29 && !(l & (1 << 24))) ++g_keyReached;
            break;
        case WM_SYSCOMMAND:
            if ((w & 0xFFF0) == SC_KEYMENU) {
                ++g_keyMenu;
                return 0;   // jamais de menu ouvert pendant l'essai
            }
            break;
        case WM_CLOSE: return 0;   // seule la sonde ferme ses fenêtres
        default: break;
    }
    return DefWindowProcW(h, m, w, l);
}

int front() {
    const HWND f = GetForegroundWindow();
    for (int i = 0; i < 3; ++i)
        if (g_windows[i] == f) return i;
    return -1;
}

INPUT vkey(WORD vk, bool up) {
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wVk = vk;
    i.ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
    return i;
}

INPUT scan(WORD code, bool up) {   // la touche sous Échap, à sa place (` en QWERTY, ² en AZERTY)
    INPUT i{};
    i.type = INPUT_KEYBOARD;
    i.ki.wScan = code;
    i.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0);
    return i;
}

// Alt (et Maj) + la touche sous Échap, appuyée presses fois sans relâcher (répétition), puis relâchée comme une main :
// Alt reste enfoncée un instant après la touche.
void chord(bool shift, int presses = 1) {
    if (g_stop) return;
    if (front() < 0) {
        wprintf(L"ARRÊT  aucune fenêtre de la sonde n'a le premier plan : aucune touche\n");
        g_stop = true;
        ++g_failures;
        return;
    }
    std::vector<INPUT> in{vkey(VK_MENU, false)};
    if (shift) in.push_back(vkey(VK_SHIFT, false));
    for (int i = 0; i < presses; ++i) in.push_back(scan(0x29, false));
    in.push_back(scan(0x29, true));
    SendInput(UINT(in.size()), in.data(), sizeof(INPUT));
    pump(150);
    in.clear();
    if (shift) in.push_back(vkey(VK_SHIFT, true));
    in.push_back(vkey(VK_MENU, true));
    SendInput(UINT(in.size()), in.data(), sizeof(INPUT));
    pump(250);
}

void expectFront(int i, const wchar_t* what) {
    if (g_stop) return;
    const int f = front();
    wchar_t text[200];
    swprintf(text, 200, L"%s (au premier plan : %d, attendu : %d)", what, f + 1, i + 1);
    check(f == i, text);
    if (f < 0) {
        wprintf(L"ARRÊT  le premier plan a quitté la sonde\n");
        g_stop = true;
    }
}

} // namespace

int wmain() {
    _setmode(_fileno(stdout), _O_U8TEXT);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacDockWindowCycleProbe";
    RegisterClassExW(&wc);
    for (int i = 0; i < 3; ++i) {   // ouvertes l'une après l'autre : l'ordre du Dock est celui-ci
        wchar_t title[64];
        swprintf(title, 64, L"Essai de la fenêtre suivante %d", i + 1);
        g_windows[i] = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, 240 + 160 * i, 200 + 120 * i, 640, 420,
                                       nullptr, nullptr, wc.hInstance, nullptr);
        ShowWindow(g_windows[i], SW_SHOWNOACTIVATE);
        pump(300);
    }
    pump(1200);   // le Dock les voit (une app de plus)
    // Premier plan pour la fenêtre 1 : une frappe neutre de la sonde lui en donne le droit.
    INPUT neutral[2] = {vkey(0xE8, false), vkey(0xE8, true)};
    for (int k = 0; k < 5 && front() != 0; ++k) {
        SendInput(2, neutral, sizeof(INPUT));
        SetForegroundWindow(g_windows[0]);
        pump(200);
    }
    check(front() == 0, L"la fenêtre 1 a le premier plan");
    if (front() != 0) g_stop = true;
    chord(false);
    expectFront(1, L"Alt+` : fenêtre 2");
    chord(false);
    expectFront(2, L"Alt+` : fenêtre 3");
    chord(false);
    expectFront(0, L"Alt+` : retour à la fenêtre 1, en boucle");
    chord(true);
    expectFront(2, L"Alt+Maj+` : fenêtre 3, la précédente");
    if (!g_stop) {
        // Réduite sans activer : SW_MINIMIZE donnerait le premier plan à la fenêtre suivante dans l'ordre Z.
        ShowWindow(g_windows[1], SW_SHOWMINNOACTIVE);   // le génie la range dans le Dock
        pump(1500);
        expectFront(2, L"fenêtre 2 réduite, la 3 garde le premier plan");
    }
    chord(false);
    expectFront(0, L"fenêtre 2 réduite : Alt+` de 3 à 1");
    chord(false);
    expectFront(2, L"Alt+` de 1 à 3, la réduite sautée");
    chord(false, 4);
    expectFront(0, L"Alt+` tenu (trois répétitions) : une seule fenêtre de plus");
    check(g_keyReached == 0, L"la touche n'est jamais arrivée aux fenêtres");
    check(g_keyMenu == 0, L"Alt relâché n'a demandé aucun menu");
    for (HWND h : g_windows) DestroyWindow(h);
    pump(300);
    wprintf(L"\n%ls : %d échec(s)\n", g_failures ? L"ÉCHEC" : L"OK", g_failures);
    return g_failures;
}
