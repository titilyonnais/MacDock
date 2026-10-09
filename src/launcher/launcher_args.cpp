#include "launcher_args.h"

namespace md {

// Chaque argument comparé en entier (« --install » n'est pas le début de « --install-update ») ; le premier connu
// décide, les autres sont ignorés.
LauncherCommand launcherCommand(const std::vector<std::wstring>& argv) {
    struct Known {
        const wchar_t* arg;
        LauncherCommand command;
    };
    static constexpr Known known[] = {
        {L"--install", LauncherCommand::Install},           {L"--uninstall", LauncherCommand::Uninstall},
        {L"--quit", LauncherCommand::Quit},                 {L"--check-update", LauncherCommand::CheckUpdate},
        {L"--install-update", LauncherCommand::InstallUpdate},
    };
    for (std::size_t i = 1; i < argv.size(); ++i)
        for (const Known& k : known)
            if (argv[i] == k.arg) return k.command;
    return LauncherCommand::Run;
}

} // namespace md
