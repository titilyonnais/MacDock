// Ligne de commande de MacDockLauncher.exe (logique pure) : la commande demandée.
#pragma once
#include <string>
#include <vector>

namespace md {

enum class LauncherCommand { Run, Install, Uninstall, Quit, CheckUpdate, InstallUpdate };

// `argv` : nom du programme compris (CommandLineToArgvW).
LauncherCommand launcherCommand(const std::vector<std::wstring>& argv);

} // namespace md
