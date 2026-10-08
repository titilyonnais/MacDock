#include "actions.h"

#include "mods.h"

namespace md {

namespace {
std::wstring parentOf(const std::wstring& path) {
    const std::size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}

ActionCommand quit(const ButtonContext& c) { return {c.exeDir + L"\\MacDock.exe", L"--quit", L"open", true}; }
ActionCommand launch(const ButtonContext& c) { return {c.exeDir + L"\\MacDockLauncher.exe", L""}; }
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
                params += c.restartExplorer ? L" -Restart" : L" -NoRestart";
                return {ActionCommand{L"powershell.exe", params, L"runas", true}};
            }
            return {};
        }
        case PaneAction::GetWindhawk: return {ActionCommand{L"https://windhawk.net", L""}};
        case PaneAction::ShowFolder: return {ActionCommand{L"explorer.exe", L"\"" + c.dataDir + L"\""}};
        case PaneAction::ShowLogs: return {ActionCommand{L"explorer.exe", L"\"" + c.dataDir + L"\\logs\""}};
        case PaneAction::None:
        case PaneAction::Export:
        case PaneAction::Import:
        case PaneAction::Reset: return {};
    }
    return {};
}

}  // namespace md
