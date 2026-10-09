// Forcer à quitter (plan 54) : apps de la liste, processus à arrêter, sélection après une relecture (logique pure) ;
// côté Windows : identité avec PID, processus de la session, arrêt d'un processus d'essai à nous, Explorateur.
#include <windows.h>

#include <string>
#include <vector>

#include "minitest.h"
#include "../src/forcequit/force_quit_logic.h"
#include "../src/forcequit/force_quit_system.h"
#include "../src/tracker/app_identity.h"

namespace {

md::FqWindow win(const wchar_t* appId, const wchar_t* name, unsigned long pid, bool hung = false,
                 const wchar_t* exe = L"C:\\Apps\\app.exe", const wchar_t* aumid = L"") {
    md::FqWindow w;
    w.appId = appId;
    w.name = name;
    w.pid = pid;
    w.hung = hung;
    w.exePath = exe;
    w.aumid = aumid;
    return w;
}

md::FqApp explorerApp() {
    md::FqApp e;
    e.appId = L"aumid:Microsoft.Windows.Explorer";
    e.name = L"Explorateur de fichiers";
    e.exePath = L"C:\\Windows\\explorer.exe";
    e.pids = {500};
    e.explorer = true;
    return e;
}

std::vector<std::wstring> names(const std::vector<md::FqApp>& apps) {
    std::vector<std::wstring> out;
    for (const auto& a : apps) out.push_back(a.name);
    return out;
}

} // namespace

TEST_CASE(force_quit_apps_grouped_sorted_explorer_last) {
    const std::vector<md::FqWindow> windows = {
        win(L"zeta", L"zeta", 30),
        win(L"edit", L"Éditeur", 20),
        win(L"alpha", L"Alpha", 10),
        win(L"alpha", L"Alpha", 11),   // deuxième processus de la même app
        win(L"alpha", L"Alpha", 10),   // deuxième fenêtre du même processus
        win(L"bravo", L"bravo", 40),
        win(L"aumid:Microsoft.Windows.Explorer", L"Explorateur de fichiers", 500, false, L"C:\\Windows\\explorer.exe"),
    };
    const auto apps = md::forceQuitApps(windows, explorerApp());
    // Ordre du français : casse et accents ignorés (É avec E) ; l'Explorateur en dernier, une seule fois.
    CHECK((names(apps) == std::vector<std::wstring>{L"Alpha", L"bravo", L"Éditeur", L"zeta", L"Explorateur de fichiers"}));
    REQUIRE(apps.size() == 5);
    CHECK((apps[0].pids == std::vector<unsigned long>{10, 11}));
    CHECK(!apps[0].explorer);
    CHECK(apps[4].explorer);
    // Sans fenêtre de l'Explorateur ouverte, il est listé quand même, comme le Finder.
    const auto noWindows = md::forceQuitApps({win(L"alpha", L"Alpha", 10)}, explorerApp());
    CHECK((names(noWindows) == std::vector<std::wstring>{L"Alpha", L"Explorateur de fichiers"}));
    // Pas d'Explorateur (bureau arrêté) : rien d'inventé.
    CHECK((names(md::forceQuitApps({win(L"alpha", L"Alpha", 10)}, md::FqApp{})) == std::vector<std::wstring>{L"Alpha"}));
}

TEST_CASE(force_quit_apps_hung_and_unknown_pid) {
    const auto apps = md::forceQuitApps({win(L"a", L"A", 10), win(L"a", L"A", 10, true), win(L"b", L"B", 0),
                                         win(L"c", L"C", 30)},
                                        md::FqApp{});
    REQUIRE(apps.size() == 3);
    CHECK(apps[0].hung);                 // une fenêtre figée suffit
    CHECK(!apps[2].hung);
    CHECK(apps[1].pids.empty());         // app du Store réduite : PID inconnu, jamais 0 dans la liste
}

TEST_CASE(force_quit_targets_never_wrong_process) {
    md::FqApp app;
    app.appId = L"aumid:Contoso.App_abc!App";
    app.aumid = L"Contoso.App_abc!App";
    app.pids = {10, 0, 4, 77};   // 0 et 4 : jamais ; 77 : le Dock lui-même
    const std::vector<md::FqProcess> processes = {
        {10, L"Contoso.App_abc!App", L"C:\\Program Files\\WindowsApps\\Contoso\\app.exe"},
        {11, L"Contoso.App_abc!App", L"C:\\Program Files\\WindowsApps\\Contoso\\helper.exe"},   // même app empaquetée
        {12, L"Autre.App_xyz!App", L"C:\\Program Files\\WindowsApps\\Autre\\autre.exe"},
        {13, L"", L"C:\\Windows\\System32\\ApplicationFrameHost.exe"},
        {14, L"Contoso.App_abc!App", L"C:\\Windows\\explorer.exe"},   // jamais l'Explorateur, même avec le même AUMID
        {77, L"", L"C:\\Apps\\MacDock\\MacDock.exe"},
    };
    CHECK((md::forceQuitTargets(app, processes, 77) == std::vector<unsigned long>{10, 11}));
    // App de bureau (pas d'AUMID empaqueté) : seulement les propriétaires de ses fenêtres.
    md::FqApp desktop;
    desktop.appId = L"exe:c:\\apps\\notes.exe";
    desktop.pids = {20, 21};
    CHECK((md::forceQuitTargets(desktop, {{20, L"", L"C:\\Apps\\notes.exe"}, {22, L"", L"C:\\Apps\\notes.exe"}}, 77) ==
           std::vector<unsigned long>{20, 21}));
    // Fenêtre d'ApplicationFrameHost ou de MacDock remontée par erreur : refusée.
    md::FqApp frame;
    frame.pids = {13, 78};
    CHECK(md::forceQuitTargets(frame, {{13, L"", L"C:\\Windows\\System32\\ApplicationFrameHost.exe"},
                                       {78, L"", L"C:\\Apps\\MacDock\\MacMenuBar.exe"}}, 77).empty());
}

TEST_CASE(force_quit_explorer_detection) {
    CHECK(md::isExplorerExe(L"C:\\Windows\\explorer.exe"));
    CHECK(md::isExplorerExe(L"c:\\windows\\EXPLORER.EXE"));
    CHECK(!md::isExplorerExe(L"C:\\Apps\\explorer2.exe"));
    CHECK(!md::isExplorerExe(L""));
}

TEST_CASE(force_quit_selection_after_reload) {
    md::FqApp a, b, c;
    a.appId = L"a";
    b.appId = L"b";
    c.appId = L"c";
    const std::vector<md::FqApp> before = {a, b, c};
    CHECK(md::selectionAfter(before, 1, {a, b, c}) == 1);   // rien n'a changé
    CHECK(md::selectionAfter(before, 1, {b, c}) == 0);      // « a » est parti : « b » reste choisie
    CHECK(md::selectionAfter(before, 1, {a, c}) == 1);      // « b » quittée : celle qui prend sa place
    CHECK(md::selectionAfter(before, 2, {a, b}) == 1);      // la dernière quittée : la nouvelle dernière
    CHECK(md::selectionAfter(before, -1, {a, b}) == -1);    // aucune sélection : aucune
    CHECK(md::selectionAfter(before, 0, {}) == -1);
}

// ---- Côté Windows ----

TEST_CASE(force_quit_identity_keeps_pid) {
    HWND w = CreateWindowExW(0, L"STATIC", L"Essai (MacDock)", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, nullptr,
                             nullptr);
    REQUIRE(w != nullptr);
    const auto id = md::identifyWindow(w);
    REQUIRE(id.has_value());
    CHECK(id->pid == GetCurrentProcessId());
    DestroyWindow(w);
}

TEST_CASE(force_quit_session_processes) {
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    bool found = false;
    for (const md::FqProcess& p : md::sessionProcesses())
        if (p.pid == GetCurrentProcessId()) found = _wcsicmp(p.exePath.c_str(), self) == 0 && p.aumid.empty();
    CHECK(found);   // ce processus, son exécutable, pas d'AUMID (pas empaqueté)
}

TEST_CASE(force_quit_terminates_own_child_only) {
    // Un processus d'essai à nous (cmd.exe qui attend, sans fenêtre visible) : arrêté, et attendu jusqu'à sa fin.
    wchar_t sys[MAX_PATH] = {};
    GetSystemDirectoryW(sys, MAX_PATH);
    const std::wstring cmd = std::wstring(sys) + L"\\cmd.exe";
    std::wstring line = L"\"" + cmd + L"\" /d /c pause";
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    REQUIRE(CreateProcessW(cmd.c_str(), line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi));
    std::wstring error;
    CHECK(md::terminateProcesses({pi.dwProcessId}, &error));
    CHECK(error.empty());
    CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0);   // déjà fini au retour
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // PID inexistant (processus déjà parti) : pas une erreur ; 0, 4 et ce processus : jamais touchés.
    CHECK(md::terminateProcesses({0xFFFFFFF0ul}, &error));
    CHECK(md::terminateProcesses({0, 4, GetCurrentProcessId()}, &error));
}

TEST_CASE(force_quit_session_explorer) {
    SKIP_ON_CI_IF(!GetShellWindow(), "pas de bureau Windows sur cette machine");
    const md::FqApp e = md::sessionExplorer();
    CHECK(!e.appId.empty());
    CHECK(e.explorer);
    CHECK(md::isExplorerExe(e.exePath));
    CHECK(!e.pids.empty());
    CHECK(e.name == L"Explorateur de fichiers");
}

TEST_CASE(force_quit_windows_never_this_process) {
    HWND w = CreateWindowExW(0, L"STATIC", L"Essai (MacDock)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, nullptr,
                             nullptr, nullptr, nullptr);
    for (const md::FqWindow& fw : md::forceQuitWindows()) CHECK(fw.pid != GetCurrentProcessId());
    DestroyWindow(w);
}
