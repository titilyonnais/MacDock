#include "force_quit_logic.h"

#include <windows.h>

#include <algorithm>

namespace md {

namespace {

std::wstring_view fileName(std::wstring_view path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring_view::npos ? path : path.substr(slash + 1);
}

bool sameText(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), int(a.size()), b.data(), int(b.size()), TRUE) == CSTR_EQUAL;
}

// Processus que Forcer à quitter n'arrête jamais : le bureau, le cadre des apps du Store, le système, MacDock.
bool protectedExe(std::wstring_view exePath) {
    const std::wstring_view name = fileName(exePath);
    for (const wchar_t* n : {L"explorer.exe", L"applicationframehost.exe", L"dwm.exe", L"csrss.exe", L"winlogon.exe",
                             L"macdock.exe", L"macmenubar.exe", L"macdocklauncher.exe"})
        if (sameText(name, n)) return true;
    return false;
}

bool collatesBefore(const std::wstring& a, const std::wstring& b) {
    return CompareStringEx(LOCALE_NAME_USER_DEFAULT, LINGUISTIC_IGNORECASE, a.c_str(), int(a.size()), b.c_str(), int(b.size()),
                           nullptr, nullptr, 0) == CSTR_LESS_THAN;
}

void sortUnique(std::vector<unsigned long>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}

int indexOf(const std::vector<FqApp>& apps, const std::wstring& appId) {
    for (std::size_t i = 0; i < apps.size(); ++i)
        if (apps[i].appId == appId) return int(i);
    return -1;
}

} // namespace

bool isExplorerExe(std::wstring_view exePath) { return !exePath.empty() && sameText(fileName(exePath), L"explorer.exe"); }

std::vector<FqApp> forceQuitApps(const std::vector<FqWindow>& windows, const FqApp& explorer) {
    std::vector<FqApp> apps;
    FqApp shell = explorer;
    for (const FqWindow& w : windows) {
        if (isExplorerExe(w.exePath)) {   // fenêtres de l'Explorateur : sa ligne, en dernier
            if (shell.appId.empty()) continue;
            if (w.pid) shell.pids.push_back(w.pid);
            shell.hung = shell.hung || w.hung;
            continue;
        }
        int at = indexOf(apps, w.appId);
        if (at < 0) {
            FqApp a;
            a.appId = w.appId;
            a.name = w.name;
            a.launch = w.launch;
            a.aumid = w.aumid;
            a.exePath = w.exePath;
            apps.push_back(std::move(a));
            at = int(apps.size()) - 1;
        }
        if (w.pid) apps[std::size_t(at)].pids.push_back(w.pid);
        apps[std::size_t(at)].hung = apps[std::size_t(at)].hung || w.hung;
    }
    for (FqApp& a : apps) sortUnique(a.pids);
    std::stable_sort(apps.begin(), apps.end(), [](const FqApp& a, const FqApp& b) { return collatesBefore(a.name, b.name); });
    if (!shell.appId.empty()) {
        shell.explorer = true;
        shell.pids.erase(std::remove(shell.pids.begin(), shell.pids.end(), 0ul), shell.pids.end());
        sortUnique(shell.pids);
        apps.push_back(std::move(shell));
    }
    return apps;
}

std::vector<unsigned long> forceQuitTargets(const FqApp& app, const std::vector<FqProcess>& processes, unsigned long self) {
    const auto allowed = [&](unsigned long pid) {
        if (pid == 0 || pid == 4 || pid == self) return false;
        for (const FqProcess& p : processes)
            if (p.pid == pid) return !protectedExe(p.exePath);
        return false;   // absent de l'instantané : processus inconnu, ou parti (son PID pourrait être repris)
    };
    std::vector<unsigned long> out;
    for (unsigned long pid : app.pids)
        if (allowed(pid)) out.push_back(pid);
    // App du Store réduite (aucune fenêtre à elle) : ses processus par son AUMID. Jamais sinon : un AUMID de paquet
    // désigne un exécutable (PowerShell 7 du Store…), pas l'app vue, et d'autres processus sans fenêtre le portent.
    if (!app.aumid.empty() && app.pids.empty())
        for (const FqProcess& p : processes)
            if (!p.aumid.empty() && sameText(p.aumid, app.aumid) && allowed(p.pid)) out.push_back(p.pid);
    sortUnique(out);
    return out;
}

int selectionAfter(const std::vector<FqApp>& before, int selected, const std::vector<FqApp>& after) {
    if (after.empty() || selected < 0 || selected >= int(before.size())) return -1;
    if (const int same = indexOf(after, before[std::size_t(selected)].appId); same >= 0) return same;
    for (std::size_t i = std::size_t(selected) + 1; i < before.size(); ++i)
        if (const int next = indexOf(after, before[i].appId); next >= 0) return next;
    for (int i = selected - 1; i >= 0; --i)
        if (const int previous = indexOf(after, before[std::size_t(i)].appId); previous >= 0) return previous;
    return std::min(selected, int(after.size()) - 1);
}

} // namespace md
