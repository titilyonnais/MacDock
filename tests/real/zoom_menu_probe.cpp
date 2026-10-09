// Essai réel du plan 51 (menu de la pastille verte) sur deux fenêtres d'essai à soi. MacMenuBar doit tourner avec
// --trace et MACDOCK_DIAG=1 : calques et menus visibles aux captures, centres des icônes écrits au journal.
//   1. survol de la pastille verte de A : pas de menu avant le délai, puis le menu sous la pastille ;
//   2. clic sur « Gauche » : A rangée à gauche, menu fermé, A au premier plan ;
//   3. nouveau survol, Échap : rien ne bouge ; le pointeur resté sur la pastille ne rouvre pas le menu ;
//   4. « Gauche et droite » : A à gauche, B (la suivante) à droite, seulement si A et B sont les deux premières
//      fenêtres admissibles de l'écran ; sinon l'essai est sauté et aucune autre fenêtre n'est touchée. B passe ensuite
//      juste sous A, devant la fenêtre outil C qui la recouvrait ;
//   5. « Plein écran » : A reçoit la même commande qu'un clic sur la pastille (WM_SYSCOMMAND SC_MAXIMIZE) ;
//   6. B activée pendant que le menu est ouvert (comme Alt+Tab) : le menu se ferme et le premier plan reste à B.
// Avant chaque clic, le pointeur doit être sur un panneau de menu de MacDock, sinon l'essai s'arrête. Le pointeur est
// remis à sa place à la fin.
// Compilation (Developer Command Prompt), depuis la racine du dépôt :
//   cl /std:c++20 /EHsc /utf-8 /DUNICODE /D_UNICODE /DNOMINMAX tests\real\zoom_menu_probe.cpp src\shell\window_tile.cpp
//      /link user32.lib gdi32.lib dwmapi.lib shell32.lib ole32.lib
#include <windows.h>
#include <dwmapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "../../src/shell/window_tile.h"

static HWND g_a, g_b, g_c;
static int failures = 0;
static int g_sysMaximize = 0;   // WM_SYSCOMMAND SC_MAXIMIZE reçus par A
static bool aborted = false;

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_DESTROY && h == g_a) PostQuitMessage(0);
    if (m == WM_SYSCOMMAND && h == g_a && (w & 0xFFF0) == SC_MAXIMIZE) ++g_sysMaximize;
    return DefWindowProcW(h, m, w, l);
}

static double now() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}

static std::wstring cls(HWND h) {
    wchar_t s[64]{};
    if (h) GetClassNameW(h, s, 64);
    return s;
}

static void check(bool ok, const char* what) {
    failures += ok ? 0 : 1;
    std::printf("%-58s %s\n", what, ok ? "OK" : "ÉCHEC");
}

static RECT frame(HWND h) {
    RECT r{};
    DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r);
    return r;
}

struct Find {
    const wchar_t* cls;
    RECT inside;
    bool needInside;
    HWND found;
};
static BOOL CALLBACK enumFind(HWND h, LPARAM lp) {
    auto* f = reinterpret_cast<Find*>(lp);
    if (!IsWindowVisible(h) || cls(h) != f->cls) return TRUE;
    if (f->needInside) {
        RECT r;
        GetWindowRect(h, &r);
        if (r.left < f->inside.left || r.top < f->inside.top || r.left > f->inside.right || r.top > f->inside.bottom) return TRUE;
    }
    f->found = h;
    return FALSE;
}
static HWND findVisible(const wchar_t* c, const RECT* inside) {
    Find f{c, inside ? *inside : RECT{}, inside != nullptr, nullptr};
    EnumWindows(enumFind, reinterpret_cast<LPARAM>(&f));
    return f.found;
}
static HWND menuPanel() { return findVisible(L"MacDockMenu", nullptr); }

static std::vector<DWORD> grab(const RECT& r) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(mem, bmp);
    BitBlt(mem, 0, 0, w, h, screen, r.left, r.top, SRCCOPY | CAPTUREBLT);
    std::vector<DWORD> px(static_cast<DWORD*>(bits), static_cast<DWORD*>(bits) + std::size_t(w) * h);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    return px;
}

static void saveBmp(const wchar_t* name, const RECT& r) {   // menu de MacDock sur la fenêtre d'essai seulement
    const auto px = grab(r);
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (FILE* f = _wfopen(name, L"wb")) {
        BITMAPFILEHEADER fh{0x4D42, DWORD(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + px.size() * 4), 0, 0,
                            sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        BITMAPINFOHEADER ih{sizeof ih, w, -h, 1, 32, BI_RGB};
        fwrite(&fh, sizeof fh, 1, f);
        fwrite(&ih, sizeof ih, 1, f);
        fwrite(px.data(), 4, px.size(), f);
        fclose(f);
    }
}

struct Blob {
    int left, right;
};
// Taches qui s'écartent de la couleur du fond (la plus fréquente) sur la ligne qui en a le plus (sonde du plan 44).
static std::vector<Blob> blobs(const std::vector<DWORD>& px, int w, int h, int* rowOut) {
    DWORD patch = 0;
    int best = 0;
    for (std::size_t i = 0; i < px.size(); i += 7) {
        int n = 0;
        for (std::size_t j = 0; j < px.size(); j += 7) n += (px[j] & 0xFFFFFF) == (px[i] & 0xFFFFFF);
        if (n > best) best = n, patch = px[i] & 0xFFFFFF;
    }
    auto off = [&](DWORD c) {
        int d = 0;
        for (int s = 0; s < 24; s += 8) d = (std::max)(d, std::abs(int((c >> s) & 0xFF) - int((patch >> s) & 0xFF)));
        return d > 24;
    };
    int row = 0, most = -1;
    for (int y = 0; y < h; ++y) {
        int n = 0;
        for (int x = 0; x < w; ++x) n += off(px[std::size_t(y) * w + x]);
        if (n > most && n < w * 7 / 10) most = n, row = y;
    }
    std::vector<Blob> out;
    for (int x = 0; x < w; ++x) {
        if (!off(px[std::size_t(row) * w + x])) continue;
        if (!out.empty() && x - out.back().right <= 2) out.back().right = x;
        else out.push_back({x, x});
    }
    *rowOut = row;
    return out;
}

// Centre de la pastille verte de la fenêtre (calque des pastilles posé sur elle), ou faux.
static bool greenLight(HWND win, POINT& out) {
    RECT wr;
    GetWindowRect(win, &wr);
    HWND layer = nullptr;
    for (int i = 0; i < 150 && !layer; ++i) {
        layer = findVisible(L"MacMenuBarLights", &wr);
        if (!layer) Sleep(20);
    }
    if (!layer) return false;
    Sleep(400);   // calque posé et peint
    RECT lr;
    GetWindowRect(layer, &lr);
    int row = 0;
    const auto b = blobs(grab(lr), lr.right - lr.left, lr.bottom - lr.top, &row);
    if (b.size() < 3) return false;
    out = {lr.left + (b[2].left + b[2].right) / 2, lr.top + row};
    return true;
}

// Centre écran d'une icône, lu au journal de la barre (dernière ouverture du menu) : « icône r.k (Titre) x=… y=… ».
static bool iconCenter(const char* key, POINT& out) {
    PWSTR roaming = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &roaming))) return false;
    const std::wstring path = std::wstring(roaming) + L"\\MacDock\\logs\\menubar\\log.txt";
    CoTaskMemFree(roaming);
    std::ifstream f(path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string s = ss.str();
    const std::size_t at = s.rfind(key);
    if (at == std::string::npos) return false;
    const std::size_t xs = s.find("x=", at);
    long x = 0, y = 0;
    if (xs == std::string::npos || sscanf_s(s.c_str() + xs, "x=%ld y=%ld", &x, &y) != 2) return false;
    out = {x, y};
    return true;
}

static HWND waitMenu(double t0, double timeout) {
    while (now() - t0 < timeout) {
        if (HWND m = menuPanel()) return m;
        Sleep(5);
    }
    return nullptr;
}
static bool waitNoMenu(double timeout) {
    const double t0 = now();
    while (now() - t0 < timeout) {
        if (!menuPanel()) return true;
        Sleep(10);
    }
    return false;
}

static bool click(POINT p, const wchar_t* shot = nullptr) {
    SetCursorPos(p.x, p.y);
    Sleep(250);   // survol : l'icône passe en bleu
    POINT c;
    GetCursorPos(&c);
    HWND under = GetAncestor(WindowFromPoint(c), GA_ROOT);
    if (cls(under) != L"MacDockMenu") {
        std::printf("pointeur hors du menu (%ls) : arrêt\n", cls(under).c_str());
        aborted = true;
        return false;
    }
    if (shot) {
        RECT mr;
        GetWindowRect(under, &mr);
        saveBmp(shot, mr);
    }
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
    return true;
}

static void key(WORD vk) {
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static POINT clientCenter(HWND h) {
    RECT r;
    GetWindowRect(h, &r);
    return {(r.left + r.right) / 2, (r.top + r.bottom) / 2 + 40};
}

// Ouvre le menu de la pastille verte de `win` : pointeur ailleurs sur la fenêtre, puis sur la pastille.
static HWND openMenu(HWND win, double* delay = nullptr, bool* early = nullptr) {
    POINT green;
    if (!greenLight(win, green)) {
        std::printf("pastille verte introuvable\n");
        return nullptr;
    }
    const POINT away = clientCenter(win);
    SetCursorPos(away.x, away.y);
    Sleep(200);
    const double t0 = now();
    SetCursorPos(green.x, green.y);
    if (early) {
        Sleep(400);
        *early = menuPanel() != nullptr;
    }
    HWND m = waitMenu(t0, 2.5);
    if (delay) *delay = now() - t0;
    return m;
}

// Fenêtres admissibles à Organiser sur l'écran de `first`, de l'avant vers l'arrière (mêmes règles que window_tile).
static BOOL CALLBACK collect(HWND h, LPARAM lp) {
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    const LONG_PTR st = GetWindowLongPtrW(h, GWL_STYLE), ex = GetWindowLongPtrW(h, GWL_EXSTYLE);
    if (IsWindowVisible(h) && !IsIconic(h) && !cloaked && (st & WS_CAPTION) == WS_CAPTION && (st & WS_THICKFRAME) &&
        !(st & WS_CHILD) && !(ex & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) && !GetWindow(h, GW_OWNER))
        reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}

static void scenario() {
    Sleep(600);
    POINT saved;
    GetCursorPos(&saved);
    SetForegroundWindow(g_a);
    MONITORINFO mi{sizeof mi};
    const HMONITOR mon = MonitorFromWindow(g_a, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    UINT dpi = 96, dpiY = 96;
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpi, &dpiY);
    const int margin = MulDiv(8, int(dpi), 96);
    const RECT original = frame(g_a);

    // 1. Survol : le menu attend son délai, puis s'ouvre sous la pastille.
    double delay = 0;
    bool early = false;
    HWND m = openMenu(g_a, &delay, &early);
    check(!early, "pas de menu avant le délai (0,4 s)");
    check(m != nullptr, "menu ouvert au survol de la pastille verte");
    if (!m) goto done;
    std::printf("délai mesuré : %.0f ms\n", delay * 1000);
    check(delay > 0.6 && delay < 1.6, "délai entre 0,6 et 1,6 s");
    {
        RECT mr;
        GetWindowRect(m, &mr);
        POINT green;
        greenLight(g_a, green);
        std::printf("menu (%ld,%ld)-(%ld,%ld), pastille (%ld,%ld)\n", mr.left, mr.top, mr.right, mr.bottom, green.x, green.y);
        check(mr.left < green.x && mr.right > green.x, "menu sous la pastille, étendu vers la gauche");
        check(mr.top > green.y - 40 && mr.top < green.y + 60, "haut du menu juste sous la pastille");
        saveBmp(L"zoom-menu.bmp", mr);
    }

    // 2. « Gauche ».
    {
        POINT p;
        if (!iconCenter(" 1.0 (Gauche) x=", p)) {
            std::printf("icône « Gauche » absente du journal (MacMenuBar --trace ?)\n");
            ++failures;
            goto done;
        }
        if (!click(p, L"zoom-hover.bmp")) goto done;
        check(waitNoMenu(1.5), "menu refermé après le choix");
        Sleep(400);
        const RECT want = md::tileRect(md::TileAction::Left, mi.rcWork, original, margin), got = frame(g_a);
        std::printf("Gauche : voulu (%ld,%ld)-(%ld,%ld) obtenu (%ld,%ld)-(%ld,%ld)\n", want.left, want.top, want.right,
                    want.bottom, got.left, got.top, got.right, got.bottom);
        check(EqualRect(&want, &got) != FALSE, "A rangée à gauche");
        check(GetForegroundWindow() == g_a, "A au premier plan");
    }

    // 3. Échap : rien ne bouge, le menu ne revient pas tant que le pointeur reste sur la pastille.
    {
        const RECT before = frame(g_a);
        m = openMenu(g_a);
        check(m != nullptr, "menu rouvert après une sortie de la pastille");
        if (!m) goto done;
        if (cls(GetForegroundWindow()) != L"MacDockMenu") {
            std::printf("le menu n'a pas le clavier : Échap non envoyé\n");
            ++failures;
            goto done;
        }
        key(VK_ESCAPE);
        check(waitNoMenu(1.5), "Échap ferme le menu");
        Sleep(300);
        const RECT after = frame(g_a);
        check(EqualRect(&before, &after) != FALSE, "Échap : fenêtre inchangée");
        check(GetForegroundWindow() == g_a, "Échap : premier plan rendu à A");
        Sleep(1500);
        check(menuPanel() == nullptr, "pointeur resté sur la pastille : pas de nouveau menu");
    }

    // 4. « Gauche et droite » : A puis B, si ce sont les deux premières fenêtres admissibles de l'écran.
    {
        std::vector<HWND> order;
        EnumWindows(collect, reinterpret_cast<LPARAM>(&order));
        std::vector<HWND> mine;
        for (HWND h : order)
            if (h != g_a && MonitorFromWindow(h, MONITOR_DEFAULTTONULL) == mon) mine.push_back(h);
        if (mine.empty() || mine.front() != g_b) {
            std::printf("Organiser sauté : une autre fenêtre précède B (%ls)\n", mine.empty() ? L"aucune" : cls(mine.front()).c_str());
        } else {
            // La fenêtre outil C (non admissible) entre A et B : après Organiser, B doit repasser devant elle.
            SetWindowPos(g_c, g_a, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SetWindowPos(g_b, g_c, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            m = openMenu(g_a);
            check(m != nullptr, "menu ouvert pour Organiser");
            if (!m) goto done;
            POINT p;
            if (!iconCenter(" 3.1 (Gauche et droite) x=", p)) {
                std::printf("icône « Gauche et droite » absente du journal\n");
                ++failures;
                goto done;
            }
            const RECT bFrame = frame(g_b);
            if (!click(p)) goto done;
            check(waitNoMenu(1.5), "menu refermé après Organiser");
            Sleep(500);
            const RECT wantA = md::tileRect(md::TileAction::Left, mi.rcWork, original, margin);
            const RECT wantB = md::tileRect(md::TileAction::Right, mi.rcWork, bFrame, margin);
            const RECT gotA = frame(g_a), gotB = frame(g_b);
            std::printf("B : voulu (%ld,%ld)-(%ld,%ld) obtenu (%ld,%ld)-(%ld,%ld)\n", wantB.left, wantB.top, wantB.right,
                        wantB.bottom, gotB.left, gotB.top, gotB.right, gotB.bottom);
            check(EqualRect(&wantA, &gotA) != FALSE, "Organiser : A à gauche");
            check(EqualRect(&wantB, &gotB) != FALSE, "Organiser : B à droite");
            HWND below = GetWindow(g_a, GW_HWNDNEXT);   // calques des pastilles et fenêtres cachées sautés
            while (below && (cls(below) == L"MacMenuBarLights" || !IsWindowVisible(below))) below = GetWindow(below, GW_HWNDNEXT);
            check(below == g_b, "Organiser : B juste sous A, devant la fenêtre outil");
        }
    }

    // 5. « Plein écran » : la même commande qu'un clic sur la pastille verte.
    {
        m = openMenu(g_a);
        check(m != nullptr, "menu ouvert pour Plein écran");
        if (!m) goto done;
        POINT p;
        if (!iconCenter(" 5 (Plein écran) x=", p)) {
            std::printf("entrée « Plein écran » absente du journal\n");
            ++failures;
            goto done;
        }
        const int before = g_sysMaximize;
        if (!click(p)) goto done;
        check(waitNoMenu(1.5), "menu refermé après Plein écran");
        Sleep(700);
        check(g_sysMaximize == before + 1, "Plein écran : commande SC_MAXIMIZE reçue par A");
        check(IsZoomed(g_a) != FALSE, "Plein écran : A agrandie");
        ShowWindow(g_a, SW_RESTORE);
        Sleep(600);
    }

    // 6. B activée pendant que le menu est ouvert (comme Alt+Tab) : le premier plan reste à B.
    {
        m = openMenu(g_a);
        check(m != nullptr, "menu ouvert (passage à une autre fenêtre)");
        if (!m) goto done;
        if (cls(GetForegroundWindow()) != L"MacDockMenu") {
            std::printf("le menu n'a pas le premier plan : essai sauté\n");
        } else {
            // Frappe qui débloque le premier plan (Alt, touche non attribuée, Alt), reçue par le menu de MacDock.
            INPUT in[4] = {};
            for (auto& i : in) i.type = INPUT_KEYBOARD;
            in[0].ki.wVk = VK_MENU;
            in[1].ki.wVk = 0xE8;
            in[2].ki.wVk = 0xE8;
            in[2].ki.dwFlags = KEYEVENTF_KEYUP;
            in[3].ki.wVk = VK_MENU;
            in[3].ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(4, in, sizeof(INPUT));
            SetForegroundWindow(g_b);
            check(waitNoMenu(1.5), "menu refermé quand B prend le premier plan");
            Sleep(700);
            check(GetForegroundWindow() == g_b, "premier plan resté à B, pas rendu à A");
        }
    }

done:
    SetCursorPos(saved.x, saved.y);
    std::printf("%s\n", aborted ? "ARRÊTÉ" : failures ? "ÉCHEC" : "tout est exact");
    PostMessageW(g_c, WM_CLOSE, 0, 0);
    PostMessageW(g_b, WM_CLOSE, 0, 0);
    PostMessageW(g_a, WM_CLOSE, 0, 0);
}

int wmain() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW wc{};
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacDockZoomMenuProbe";
    RegisterClassW(&wc);
    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    // Au premier plan des fenêtres (WS_EX_TOPMOST) : la pastille n'est jamais cachée par une fenêtre de l'utilisateur.
    g_b = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Essai B (MacDock)", WS_OVERLAPPEDWINDOW, work.left + 420,
                          work.top + 300, 700, 460, nullptr, nullptr, wc.hInstance, nullptr);
    g_a = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, L"Essai de la pastille verte (MacDock)", WS_OVERLAPPEDWINDOW,
                          work.left + 260, work.top + 220, 900, 560, nullptr, nullptr, wc.hInstance, nullptr);
    // Fenêtre outil (jamais organisée), posée là où B ira : B doit repasser devant elle.
    g_c = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName, L"Essai C (MacDock)",
                          WS_POPUP | WS_BORDER, (work.left + work.right) / 2 + 100, work.top + 200, 500, 400, nullptr, nullptr,
                          wc.hInstance, nullptr);
    ShowWindow(g_b, SW_SHOWNOACTIVATE);
    ShowWindow(g_c, SW_SHOWNOACTIVATE);
    ShowWindow(g_a, SW_SHOWNORMAL);
    std::thread t(scenario);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    t.join();
    return failures || aborted ? 1 : 0;
}
