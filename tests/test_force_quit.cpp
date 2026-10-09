// Forcer à quitter (plan 54) : apps de la liste, processus à arrêter, sélection après une relecture (logique pure) ;
// côté Windows : identité avec PID, processus de la session, arrêt d'un processus d'essai à nous, Explorateur.
#include <windows.h>
#pragma warning(push)
#pragma warning(disable : 4005)   // les en-têtes Shell redéfinissent PID_FIRST_USABLE
#include <shellapi.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propkey.h>
#include <propvarutil.h>
#pragma warning(pop)
#include <wrl/client.h>

#include <string>
#include <vector>

#include "minitest.h"
#include "../src/forcequit/force_quit_logic.h"
#include "../src/forcequit/force_quit_system.h"
#include "../src/forcequit/force_quit_view.h"
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
    // Propriétaires de ses fenêtres seulement : les autres processus de même AUMID n'ont rien à voir (relecture).
    CHECK((md::forceQuitTargets(app, processes, 77) == std::vector<unsigned long>{10}));
    // App du Store réduite (aucune fenêtre à elle) : ses processus par son AUMID, jamais l'Explorateur.
    md::FqApp minimized = app;
    minimized.pids.clear();
    CHECK((md::forceQuitTargets(minimized, processes, 77) == std::vector<unsigned long>{10, 11}));
    // App de bureau : seulement les propriétaires de ses fenêtres ; un PID absent de l'instantané (processus protégé,
    // ou parti puis repris) n'est jamais visé.
    md::FqApp desktop;
    desktop.appId = L"exe:c:\\apps\\notes.exe";
    desktop.pids = {20, 21};
    CHECK((md::forceQuitTargets(desktop, {{20, L"", L"C:\\Apps\\notes.exe"}, {22, L"", L"C:\\Apps\\notes.exe"}}, 77) ==
           std::vector<unsigned long>{20}));
    // Fenêtre d'ApplicationFrameHost ou de MacDock remontée par erreur : refusée.
    md::FqApp frame;
    frame.pids = {13, 78};
    CHECK(md::forceQuitTargets(frame, {{13, L"", L"C:\\Windows\\System32\\ApplicationFrameHost.exe"},
                                       {78, L"", L"C:\\Apps\\MacDock\\MacMenuBar.exe"}}, 77).empty());
}

TEST_CASE(force_quit_targets_package_shared_by_tools) {   // relecture du plan 54, critique 1
    // Un AUMID de paquet désigne un exécutable (PowerShell 7 du Store), pas l'app vue : deux PowerShell le portent,
    // une seule fenêtre à forcer ; l'autre, sans fenêtre (un script, un outil), n'est jamais visé.
    md::FqApp pwsh;
    pwsh.appId = L"aumid:Microsoft.PowerShell_8wekyb3d8bbwe!App";
    pwsh.aumid = L"Microsoft.PowerShell_8wekyb3d8bbwe!App";
    pwsh.pids = {16032};
    const std::wstring exe = L"C:\\Program Files\\WindowsApps\\Microsoft.PowerShell\\pwsh.exe";
    CHECK((md::forceQuitTargets(pwsh, {{16032, pwsh.aumid, exe}, {12200, pwsh.aumid, exe}}, 1) ==
           std::vector<unsigned long>{16032}));
}

TEST_CASE(force_quit_shared_processes_named) {   // relecture du plan 54, important 2
    // Une application web d'Edge ou de Chrome (son propre AUMID, sa ligne) : ses fenêtres appartiennent au navigateur.
    // La forcer arrête aussi le navigateur : la confirmation le nomme.
    md::FqApp notes, chrome, pwa;
    notes.appId = L"notes";
    notes.name = L"Bloc-notes";
    notes.pids = {200};
    chrome.appId = L"chrome";
    chrome.name = L"Google Chrome";
    chrome.pids = {100, 101};
    pwa.appId = L"pwa";
    pwa.name = L"YouTube";
    pwa.pids = {100};
    const std::vector<md::FqApp> apps = {notes, chrome, pwa};
    CHECK((md::appsSharingProcesses(apps, 2) == std::vector<std::wstring>{L"Google Chrome"}));
    CHECK((md::appsSharingProcesses(apps, 1) == std::vector<std::wstring>{L"YouTube"}));
    CHECK(md::appsSharingProcesses(apps, 0).empty());
    CHECK(md::appsSharingProcesses(apps, 7).empty());
    const md::ui::SheetSpec one = md::forceQuitConfirm(pwa, {L"Google Chrome"});
    CHECK(one.title == L"Voulez-vous forcer « YouTube » à quitter ?");
    CHECK(one.message == L"Les modifications non enregistrées seront perdues. Google Chrome quittera aussi : "
                         L"elles partagent le même processus.");
    const md::ui::SheetSpec two = md::forceQuitConfirm(chrome, {L"YouTube", L"Gmail"});
    CHECK(two.message == L"Les modifications non enregistrées seront perdues. YouTube et Gmail quitteront aussi : "
                         L"elles partagent le même processus.");
    CHECK(md::forceQuitConfirm(notes, {}).message == L"Les modifications non enregistrées seront perdues.");
    CHECK((md::forceQuitConfirm(notes, {}).buttons == std::vector<std::wstring>{L"Annuler", L"Forcer à quitter"}));
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

TEST_CASE(identity_unregistered_aumid_launches_exe) {
    // Plan 54 (essai réel) : une app de bureau dont l'AUMID explicite n'est pas inscrit (un Chrome lancé par un outil)
    // est relancée par son exécutable, et son icône vient de lui (avant : shell:AppsFolder\<AUMID>, ni icône ni
    // relance possibles).
    HWND w = CreateWindowExW(0, L"STATIC", L"Essai (MacDock)", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, nullptr,
                             nullptr);
    REQUIRE(w != nullptr);
    Microsoft::WRL::ComPtr<IPropertyStore> store;
    REQUIRE(SUCCEEDED(SHGetPropertyStoreForWindow(w, IID_PPV_ARGS(&store))));
    PROPVARIANT pv;
    REQUIRE(SUCCEEDED(InitPropVariantFromString(L"MacDock.Essai.NonInscrit", &pv)));
    REQUIRE(SUCCEEDED(store->SetValue(PKEY_AppUserModel_ID, pv)));
    PropVariantClear(&pv);
    const auto id = md::identifyWindow(w);
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    REQUIRE(id.has_value());
    CHECK(id->aumid == L"MacDock.Essai.NonInscrit");   // le regroupement ne change pas
    CHECK(_wcsicmp(id->launch.c_str(), self) == 0);
    CHECK(!id->displayName.empty());
    PropVariantInit(&pv);
    store->SetValue(PKEY_AppUserModel_ID, pv);   // propriété retirée avant la fermeture
    store.Reset();
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

TEST_CASE(force_quit_session_processes_keep_protected) {   // relecture du plan 54, important 1
    // csrss (session illisible sans droits élevés) reste dans l'instantané, par son nom : la liste des processus
    // jamais arrêtés le reconnaît (sinon son PID passerait pour inconnu).
    bool csrss = false;
    for (const md::FqProcess& p : md::sessionProcesses()) {
        const std::size_t slash = p.exePath.find_last_of(L"\\/");
        const std::wstring name = slash == std::wstring::npos ? p.exePath : p.exePath.substr(slash + 1);
        csrss = csrss || _wcsicmp(name.c_str(), L"csrss.exe") == 0;
    }
    CHECK(csrss);
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
    // Relecture du plan 54, critique 2 : le PID est maintenant à un autre programme que celui relu (PID repris) :
    // jamais arrêté, et ce n'est pas une erreur (l'app visée est partie).
    CHECK(md::terminateProcesses({{pi.dwProcessId, L"", L"C:\\Autre\\programme.exe"}}, &error));
    CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT);
    CHECK(md::terminateProcesses({{pi.dwProcessId, L"", cmd}}, &error));
    CHECK(error.empty());
    CHECK(WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0);   // déjà fini au retour
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    // PID inexistant (processus déjà parti) : pas une erreur ; 0, 4 et ce processus : jamais touchés.
    CHECK(md::terminateProcesses({{0xFFFFFFF0ul, L"", cmd}}, &error));
    CHECK(md::terminateProcesses({{0, L"", cmd}, {4, L"", cmd}, {GetCurrentProcessId(), L"", cmd}}, &error));
}

TEST_CASE(force_quit_now_app_gone_is_not_an_error) {   // relecture du plan 54, critique 2
    // Confirmation d'une app partie entre-temps (fermée, plantée) : rien d'arrêté, rien de signalé ; jamais les PID
    // gardés d'une lecture ancienne (ils peuvent avoir été repris).
    std::wstring error;
    CHECK(md::forceQuitNow(L"exe:c:\\nulle\\part\\disparue.exe", &error));
    CHECK(error.empty());
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

TEST_CASE(force_quit_preselected_app) {
    // L'app au premier plan à l'ouverture est choisie d'abord : même appId que dans la liste ; le bureau, l'Explorateur.
    HWND w = CreateWindowExW(0, L"STATIC", L"Essai (MacDock)", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, nullptr,
                             nullptr);
    REQUIRE(w != nullptr);
    CHECK(md::forceQuitAppIdOf(w) == md::identifyWindow(w)->appId);
    DestroyWindow(w);
    CHECK(md::forceQuitAppIdOf(nullptr).empty());
    SKIP_ON_CI_IF(!GetShellWindow(), "pas de bureau Windows sur cette machine");
    CHECK(md::forceQuitAppIdOf(GetShellWindow()) == md::sessionExplorer().appId);
}

TEST_CASE(force_quit_windows_never_this_process) {
    HWND w = CreateWindowExW(0, L"STATIC", L"Essai (MacDock)", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 100, 100, nullptr,
                             nullptr, nullptr, nullptr);
    for (const md::FqWindow& fw : md::forceQuitWindows()) CHECK(fw.pid != GetCurrentProcessId());
    DestroyWindow(w);
}
