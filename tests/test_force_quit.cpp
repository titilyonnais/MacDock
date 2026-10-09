// Forcer à quitter (plan 54), logique pure : apps de la liste, processus à arrêter, sélection après une relecture.
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/forcequit/force_quit_logic.h"

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
