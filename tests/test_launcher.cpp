// Lanceur (plan 53, relecture) : ligne de commande comparée argument par argument.
#include <string>
#include <vector>

#include "minitest.h"
#include "../src/launcher/launcher_args.h"

TEST_CASE(launcher_command_exact_arguments) {   // relecture du plan 53, critique 1
    using V = std::vector<std::wstring>;
    using C = md::LauncherCommand;
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe"}) == C::Run);
    CHECK(md::launcherCommand(V{}) == C::Run);
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--install"}) == C::Install);
    // « Installer » dans Réglages : jamais pris pour --install (démarrage avec Windows rallumé, rien d'installé).
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--install-update"}) == C::InstallUpdate);
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--uninstall"}) == C::Uninstall);
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--quit"}) == C::Quit);
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--check-update"}) == C::CheckUpdate);
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--installer"}) == C::Run);       // inconnu : lancement normal
    CHECK(md::launcherCommand(V{L"MacDockLauncher.exe", L"--quit-now"}) == C::Run);
    CHECK(md::launcherCommand(V{L"C:\\--install\\MacDockLauncher.exe"}) == C::Run);        // le chemin ne compte pas
}
