// Modèle de l'app Réglages : réglages du Dock (settings.json) et de la barre (menubar.json), relus juste avant chaque
// écriture et fusionnés clé par clé, pour ne jamais écraser ce que le Dock ou la barre y ont écrit entre-temps.
#pragma once
#include <functional>
#include <optional>
#include <string>

#include "../config/config_store.h"
#include "../config/settings.h"
#include "../core/json.h"
#include "../menubar/menubar_settings.h"

namespace md {

struct SettingsModel {
    Settings dock;
    MenuBarSettings bar;
    bool startup = false;   // ouvrir MacDock à l'ouverture de session (hors fichiers : SettingsIo)
};

// Ce que le modèle lit et écrit hors de ses fichiers : la valeur « MacDock » de HKCU\...\Run (démarrage avec Windows),
// la même que MacDockLauncher.exe --install. Les tests passent une version simulée.
struct SettingsIo {
    std::wstring launcherPath;   // MacDockLauncher.exe
    std::function<std::optional<std::wstring>()> readStartup;
    std::function<bool(const std::optional<std::wstring>&)> writeStartup;   // nullopt : retirée
};
SettingsIo registryIo(const std::wstring& launcherPath);   // le vrai registre (HKCU)
// Mode d'essai (--data) : la même valeur gardée dans un fichier, jamais dans le vrai registre.
SettingsIo fileStartupIo(const std::wstring& file, const std::wstring& launcherPath);

// Écritures par différence (mergeChanged) : voir config_store.h, partagé avec le Dock.

// État des fichiers lus : invalide (JSON cassé) ou illisible (verrou…). Un tel fichier n'est jamais réécrit : le Dock
// garde alors ses réglages actuels, et l'app ne doit pas le remplacer par un fichier presque vide.
struct ModelFiles {
    bool dockInvalid = false, barInvalid = false;
};

// Fichiers absents : réglages par défaut ; sans `io`, le démarrage avec Windows reste à faux.
SettingsModel loadModel(const std::wstring& dir, ModelFiles* status = nullptr, const SettingsIo* io = nullptr);

// Recharge les fichiers de `dir`, applique `edit`, puis écrit (atomiquement) seulement ceux qui changent. false si une
// écriture échoue ou si un fichier à écrire est invalide ou illisible ; `result` reçoit le modèle obtenu.
bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result = nullptr,
            const SettingsIo* io = nullptr);

} // namespace md
