// Essai réel de « Forcer à quitter » (plan 54), sans jamais toucher aux apps de l'utilisateur. La sonde lance deux apps
// d'essai à elle (des copies d'elle-même : « essai-fige », qui ne traite plus ses messages, et « essai-vivant »),
// héberge la vraie fenêtre (vraie liste des apps ouvertes, vrais clics, vraies touches) et lui donne une action gardée
// qui ne peut arrêter que ces deux processus : toute autre cible est refusée et compte comme un échec.
//   1. liste : les deux apps d'essai, la figée marquée « ne répond pas » (aussi après une fenêtre fantôme de Windows),
//      l'Explorateur en dernier, ni doublon ni app fantôme ;
//   2. clic sur la ligne de l'app figée : choisie ; clic sur « Forcer à quitter » : la confirmation la nomme ; Échap :
//      rien n'est arrêté ;
//   3. Entrée, Entrée : l'app figée est arrêtée, la sélection passe à une autre ;
//   4. ↓ ↑ ; l'app vivante choisie au clavier, confirmée à la souris : arrêtée ;
//   5. Échap ferme la fenêtre.
// Avant chaque clic, le point visé doit être sur une fenêtre de la sonde ou de ses apps d'essai ; avant chaque touche,
// la fenêtre de la sonde doit avoir le premier plan ; sinon l'essai s'arrête. Le pointeur est remis à sa place.
// Construction : ./build.ps1 -Target forcequitprobe, puis build\Debug\force_quit_probe.exe
#include <windows.h>

#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "../../src/core/log.h"
#include "../../src/forcequit/force_quit_system.h"
#include "../../src/forcequit/force_quit_window.h"

namespace {

int g_failures = 0;
bool g_stop = false;   // un clic ou une touche n'aurait pas visé la sonde : plus aucune action

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

POINT center(const RECT& r) { return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2}; }

// Fenêtre (racine) sous le point : à la sonde ou à une de ses apps d'essai ?
bool ours(POINT p, const std::set<DWORD>& mine) {
    HWND w = GetAncestor(WindowFromPoint(p), GA_ROOT);
    DWORD pid = 0;
    GetWindowThreadProcessId(w, &pid);
    return pid == GetCurrentProcessId() || mine.count(pid);
}

void click(POINT p, const std::set<DWORD>& mine) {
    if (g_stop) return;
    if (!ours(p, mine)) {
        wprintf(L"ARRÊT  le point %ld,%ld n'est pas sur une fenêtre de la sonde : aucun clic\n", p.x, p.y);
        g_stop = true;
        ++g_failures;
        return;
    }
    SetCursorPos(p.x, p.y);
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_MOUSE;
    in[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    in[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(2, in, sizeof(INPUT));
}

void key(WORD vk, HWND window) {
    if (g_stop) return;
    if (GetForegroundWindow() != window) {
        wprintf(L"ARRÊT  la fenêtre de la sonde n'a pas le premier plan : aucune touche\n");
        g_stop = true;
        ++g_failures;
        return;
    }
    INPUT in[2] = {};
    in[0].type = in[1].type = INPUT_KEYBOARD;
    in[0].ki.wVk = in[1].ki.wVk = vk;
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

int indexOf(const std::vector<md::FqApp>& apps, const wchar_t* name) {
    for (std::size_t i = 0; i < apps.size(); ++i)
        if (apps[i].name == name) return int(i);
    return -1;
}

HWND windowOf(DWORD pid) {
    struct Ctx { DWORD pid; HWND found; } ctx{pid, nullptr};
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<Ctx*>(lp);
            DWORD p = 0;
            GetWindowThreadProcessId(h, &p);
            if (p == c->pid && IsWindowVisible(h)) {
                c->found = h;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ctx));
    return ctx.found;
}

// App d'essai : une fenêtre ordinaire ; `hang` : après son premier affichage, plus aucun message (« ne répond pas »).
int child(const wchar_t* title, bool hang, int y) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"MacDockEssaiForcerAQuitter";
    RegisterClassW(&wc);
    CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, y, 420, 220, nullptr, nullptr,
                    wc.hInstance, nullptr);
    pump(600);
    if (hang) Sleep(INFINITE);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}

struct Child {
    std::wstring exe;
    PROCESS_INFORMATION pi{};
};

bool start(Child& c, const wchar_t* title, bool hang, int y) {
    std::wstring line = L"\"" + c.exe + L"\" --child \"" + title + L"\" " + (hang ? L"1 " : L"0 ") + std::to_wstring(y);
    STARTUPINFOW si{sizeof si};
    return CreateProcessW(c.exe.c_str(), line.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &c.pi) != FALSE;
}

bool alive(const Child& c) { return c.pi.hProcess && WaitForSingleObject(c.pi.hProcess, 0) == WAIT_TIMEOUT; }

} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc >= 5 && wcscmp(argv[1], L"--child") == 0) return child(argv[2], wcscmp(argv[3], L"1") == 0, _wtoi(argv[4]));
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t tmp[MAX_PATH] = {}, self[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    const std::wstring dir = std::wstring(tmp) + L"macdock-forcequit-probe";
    CreateDirectoryW(dir.c_str(), nullptr);
    md::log::init(dir + L"\\logs");
    POINT cursor{};
    GetCursorPos(&cursor);

    Child fige{dir + L"\\essai-fige.exe"}, vivant{dir + L"\\essai-vivant.exe"};
    CopyFileW(self, fige.exe.c_str(), FALSE);
    CopyFileW(self, vivant.exe.c_str(), FALSE);
    check(start(fige, L"Essai figé (MacDock)", true, 120) && start(vivant, L"Essai vivant (MacDock)", false, 380),
          L"deux apps d'essai lancées");
    const std::set<DWORD> mine{fige.pi.dwProcessId, vivant.pi.dwProcessId};
    std::vector<std::wstring> refused, stopped;

    pump(6500);   // plus de 5 s sans message : Windows la dit « ne répond pas »
    HWND hung = windowOf(fige.pi.dwProcessId);
    check(hung && IsHungAppWindow(hung), L"Windows dit l'app d'essai figée « ne répond pas »");
    if (hung) {   // un clic sur sa barre de titre : Windows pose une fenêtre fantôme à sa place
        RECT r{};
        GetWindowRect(hung, &r);
        const POINT title{r.left + 120, r.top + 12};
        if (ours(title, mine)) click(title, mine);
        else wprintf(L"info   barre de titre déjà couverte (fenêtre fantôme ?) : pas de clic\n");
        pump(1500);
    }
    bool ghost = false;
    EnumWindows(
        [](HWND h, LPARAM lp) -> BOOL {
            wchar_t cls[16] = {};
            GetClassNameW(h, cls, 16);
            if (wcscmp(cls, L"Ghost") == 0 && IsWindowVisible(h)) *reinterpret_cast<bool*>(lp) = true;
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&ghost));
    wprintf(L"info   fenêtre fantôme de Windows : %s\n", ghost ? L"oui" : L"non");

    md::ForceQuitWindow fq;
    md::ForceQuitWindow::Env env;
    env.list = [] { return md::forceQuitApps(md::forceQuitWindows(), md::sessionExplorer()); };
    env.forceQuit = [&](const md::FqApp& app, std::wstring* error) {
        if (app.explorer) {   // jamais l'Explorateur de l'utilisateur
            refused.push_back(app.name);
            if (error) *error = L"Refusé par la sonde.";
            return false;
        }
        bool allowed = false;
        const bool ok = md::forceQuitNow(app.appId, error, [&](const std::vector<md::FqProcess>& targets) {
            allowed = true;
            for (const md::FqProcess& p : targets) allowed = allowed && mine.count(p.pid) != 0;
            if (!allowed) refused.push_back(app.name);   // jamais une app de l'utilisateur
            return allowed;
        });
        if (ok && allowed) stopped.push_back(app.name);
        return ok;
    };
    env.shortcutLabel = L"⌥⊞⎋";
    check(fq.open(GetModuleHandleW(nullptr), env), L"fenêtre ouverte");
    pump(1500);

    auto apps = fq.apps();
    for (const md::FqApp& a : apps) wprintf(L"       %s%s\n", a.name.c_str(), a.hung ? L" (ne répond pas)" : L"");
    int iFige = indexOf(apps, L"essai-fige"), iVivant = indexOf(apps, L"essai-vivant");
    check(iFige >= 0 && iVivant >= 0, L"les deux apps d'essai sont listées");
    check(iFige >= 0 && apps[std::size_t(iFige)].hung, L"l'app figée est marquée « ne répond pas »");
    check(iVivant >= 0 && !apps[std::size_t(iVivant)].hung, L"l'app vivante ne l'est pas");
    check(!apps.empty() && apps.back().explorer, L"l'Explorateur est en dernier");
    std::set<std::wstring> ids;
    bool dwm = false;
    for (const md::FqApp& a : apps) {
        ids.insert(a.appId);
        dwm = dwm || a.exePath.find(L"dwm.exe") != std::wstring::npos;
    }
    check(ids.size() == apps.size() && !dwm, L"ni doublon ni app fantôme (dwm)");

    // 2. Ligne de l'app figée, « Forcer à quitter », Échap.
    if (iFige >= 0) click(center(fq.rowOnScreen(iFige)), mine);
    pump(400);
    check(fq.selected() == iFige && GetForegroundWindow() == fq.hwnd(), L"clic sur sa ligne : choisie, fenêtre au premier plan");
    click(center(fq.buttonOnScreen()), mine);
    pump(500);
    check(fq.sheetOpen() && fq.sheetTitle() == L"Voulez-vous forcer « essai-fige » à quitter ?",
          L"« Forcer à quitter » : la confirmation la nomme");
    key(VK_ESCAPE, fq.hwnd());
    pump(400);
    check(!fq.sheetOpen() && alive(fige), L"Échap : confirmation fermée, rien d'arrêté");

    // 3. Entrée, Entrée.
    key(VK_RETURN, fq.hwnd());
    pump(500);
    check(fq.sheetOpen(), L"Entrée : la confirmation revient");
    key(VK_RETURN, fq.hwnd());
    pump(800);
    check(fige.pi.hProcess && WaitForSingleObject(fige.pi.hProcess, 3000) == WAIT_OBJECT_0, L"Entrée : l'app figée est arrêtée");
    pump(1500);
    apps = fq.apps();
    check(indexOf(apps, L"essai-fige") < 0, L"elle a quitté la liste");
    check(fq.selected() >= 0 && fq.selected() < int(apps.size()), L"la sélection passe à une autre app");

    // 4. ↓ ↑, puis l'app vivante choisie au clavier et confirmée à la souris.
    key(VK_HOME, fq.hwnd());
    pump(200);
    check(fq.selected() == 0, L"Début : la première");
    key(VK_DOWN, fq.hwnd());
    pump(200);
    check(apps.size() < 2 || fq.selected() == 1, L"↓ : la suivante");
    key(VK_UP, fq.hwnd());
    pump(200);
    check(fq.selected() == 0, L"↑ : retour");
    iVivant = indexOf(fq.apps(), L"essai-vivant");
    for (int i = 0; i < 60 && !g_stop && fq.selected() != iVivant && iVivant >= 0; ++i) {
        key(VK_DOWN, fq.hwnd());
        pump(120);
    }
    check(iVivant >= 0 && fq.selected() == iVivant, L"l'app vivante choisie au clavier");
    click(center(fq.buttonOnScreen()), mine);
    pump(500);
    check(fq.sheetOpen(), L"confirmation ouverte à la souris");
    click(center(fq.sheetButtonOnScreen(1)), mine);
    pump(800);
    check(vivant.pi.hProcess && WaitForSingleObject(vivant.pi.hProcess, 3000) == WAIT_OBJECT_0,
          L"clic sur « Forcer à quitter » : l'app vivante est arrêtée");

    // 5. Taille fixe (ni bord tiré, ni Aero Snap) ; Échap ferme la fenêtre ; rouverte, Alt+F4 aussi (posté à elle seule).
    pump(1200);
    RECT before{}, after{};
    GetWindowRect(fq.hwnd(), &before);
    SetWindowPos(fq.hwnd(), nullptr, 0, 0, (before.right - before.left) * 2, (before.bottom - before.top) * 2,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    GetWindowRect(fq.hwnd(), &after);
    check(after.right - after.left == before.right - before.left && after.bottom - after.top == before.bottom - before.top,
          L"taille fixe : un agrandissement est refusé");
    key(VK_ESCAPE, fq.hwnd());
    pump(400);
    check(!fq.isOpen(), L"Échap : fenêtre fermée");
    fq.open(GetModuleHandleW(nullptr), env);
    pump(800);
    if (fq.isOpen()) PostMessageW(fq.hwnd(), WM_SYSKEYDOWN, VK_F4, LPARAM(1) | (LPARAM(1) << 29));   // Alt+F4, à elle seule
    pump(500);
    check(!fq.isOpen(), L"Alt+F4 : fenêtre fermée");

    check(refused.empty(), L"aucune autre app visée (la garde n'a rien refusé)");
    check(stopped == std::vector<std::wstring>({L"essai-fige", L"essai-vivant"}), L"seules les deux apps d'essai arrêtées");

    for (Child* c : {&fige, &vivant}) {   // ménage : jamais que nos propres processus
        if (alive(*c)) TerminateProcess(c->pi.hProcess, 1);
        if (c->pi.hProcess) {
            WaitForSingleObject(c->pi.hProcess, 3000);
            CloseHandle(c->pi.hProcess);
            CloseHandle(c->pi.hThread);
        }
        DeleteFileW(c->exe.c_str());
    }
    SetCursorPos(cursor.x, cursor.y);
    wprintf(L"\n%s\n", g_failures ? L"ÉCHECS" : L"tout est exact");
    return g_failures ? 1 : 0;
}
