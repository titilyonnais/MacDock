// Mises à jour automatiques (plan 53) : recherche sur GitHub, téléchargement vérifié, installation silencieuse. Sert au
// lanceur (fil de fond, démarrage, --check-update, --install-update) ; l'état est gardé dans update.json, que l'app
// Réglages lit. Un seul processus à la fois (mutex Local\MacDockUpdate).
#pragma once
#include <string>

#include "update_logic.h"

namespace md::update {

struct Paths {
    std::wstring stateFile;     // %APPDATA%\MacDock\update.json
    std::wstring downloadDir;   // %LOCALAPPDATA%\MacDock\updates
};
// Essais : MACDOCK_UPDATE_DIR=<dossier> y met l'état (update.json) et les téléchargements (updates), loin des vrais.
Paths defaultPaths();

struct Options {
    std::wstring repo = L"titilyonnais/MacDock";
    bool prerelease = false;   // essais : MACDOCK_UPDATE_PRERELEASE=1
};
Options optionsFromEnvironment();

UpdateState load(const Paths& p);
bool save(const Paths& p, const UpdateState& s);

enum class CheckResult { UpToDate, Ready, Failed };
// Cherche la dernière version ; plus récente : vérifie la signature de SHA256SUMS.txt (clé des versions), télécharge
// l'installateur (ou garde celui déjà prêt), vérifie son empreinte et le note prêt. L'état garde l'heure de la
// recherche et la dernière erreur. `ready` : la version prête.
CheckResult check(const Paths& p, const Options& o, std::wstring* ready = nullptr);

// Lance l'installateur prêt en silence (empreinte revérifiée juste avant) et note la tentative. `relaunch` : MacDock
// relancé même si l'installation échoue (au démarrage, rien ne tourne encore). false : rien de prêt, fichier altéré.
bool launchInstaller(const Paths& p, bool relaunch);

// Au démarrage du lanceur, avant le Dock : installe la version prête (true : l'installateur est parti, le lanceur
// s'arrête), ou oublie une version périmée ou une tentative ratée.
bool installAtStartup(const Paths& p);

} // namespace md::update
