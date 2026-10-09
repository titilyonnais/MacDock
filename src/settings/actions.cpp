#include "actions.h"

#include <windows.h>

#include <algorithm>

#include "mods.h"

namespace md {

namespace {
std::wstring parentOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

ActionCommand quit(const ButtonContext& c) {
    ActionCommand q{c.exeDir + L"\\MacDock.exe", L"--quit", L"open", true};
    q.waitStopped = true;
    return q;
}
ActionCommand launch(const ButtonContext& c) { return {c.exeDir + L"\\MacDockLauncher.exe", L""}; }

std::wstring powershell(const ButtonContext& c) {
    return c.systemDir.empty() ? L"powershell.exe" : c.systemDir + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
}
std::wstring explorer(const ButtonContext& c) {
    return c.windowsDir.empty() ? L"explorer.exe" : c.windowsDir + L"\\explorer.exe";
}
}  // namespace

std::vector<ActionCommand> actionCommands(PaneAction action, const ButtonContext& c) {
    switch (action) {
        case PaneAction::Launch: return {launch(c)};
        case PaneAction::Quit: return {quit(c)};
        case PaneAction::Restart: return {quit(c), launch(c)};
        case PaneAction::InstallMod:
        case PaneAction::UninstallMod: {
            for (const ModInfo& mod : macdockMods()) {
                if (mod.id != c.arg) continue;
                // Le script est à côté de la source du mod (livrée avec l'app, ou dans le dépôt).
                std::wstring dir = parentOf(modSourcePath(c.exeDir, mod.id));
                if (dir.empty()) dir = c.exeDir + L"\\windhawk";
                std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -File \"" + dir + L"\\" + mod.script + L"\"";
                if (action == PaneAction::UninstallMod) params += L" -Uninstall";
                params += L" -NoRestart";   // l'Explorateur : redémarré ensuite par l'app, sans droits élevés
                std::vector<ActionCommand> out{ActionCommand{powershell(c), params, L"runas", true}};
                if (action == PaneAction::InstallMod && c.restartExplorer) {
                    ActionCommand restart;
                    restart.restartExplorer = true;
                    out.push_back(restart);
                }
                return out;
            }
            return {};
        }
        case PaneAction::GetWindhawk: return {ActionCommand{L"https://windhawk.net", L""}};
        case PaneAction::ShowFolder: return {ActionCommand{explorer(c), L"\"" + c.dataDir + L"\""}};
        case PaneAction::ShowLogs: return {ActionCommand{explorer(c), L"\"" + c.dataDir + L"\\logs\""}};
        case PaneAction::CheckUpdate: {
            ActionCommand check{c.exeDir + L"\\MacDockLauncher.exe", L"--check-update", L"open", true};
            check.ignoreExitCode = true;
            check.abandonOnClose = true;
            return {check};
        }
        case PaneAction::InstallUpdate: return {ActionCommand{c.exeDir + L"\\MacDockLauncher.exe", L"--install-update"}};
        case PaneAction::None:
        case PaneAction::Export:
        case PaneAction::Import:
        case PaneAction::Reset: return {};
    }
    return {};
}

bool waitForExit(void* process, unsigned timeoutMs, const std::atomic<bool>* abandon) {
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {   // par tranches de 100 ms : la fermeture de l'app est vue aussitôt
        const ULONGLONG now = GetTickCount64();
        const DWORD slice = now >= deadline ? 0 : DWORD(std::min<ULONGLONG>(100, deadline - now));
        if (WaitForSingleObject(static_cast<HANDLE>(process), slice) == WAIT_OBJECT_0) return true;
        if (now >= deadline || (abandon && abandon->load())) return false;
    }
}

const std::vector<std::wstring>& macdockMutexes() {
    static const std::vector<std::wstring> names{L"Local\\MacDockLauncher", L"Local\\MacDock", L"Local\\MacMenuBar"};
    return names;
}

const std::vector<std::wstring>& macdockWindows() {
    static const std::vector<std::wstring> names{L"MacDockWindow", L"MacMenuBarWindow"};
    return names;
}

}  // namespace md
