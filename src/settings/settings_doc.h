// Modèle de l'app Réglages : réglages du Dock (settings.json) et de la barre (menubar.json), relus juste avant chaque
// écriture et fusionnés clé par clé, pour ne jamais écraser ce que le Dock ou la barre y ont écrit entre-temps.
#pragma once
#include <functional>
#include <string>

#include "../config/settings.h"
#include "../core/json.h"
#include "../menubar/menubar_settings.h"

namespace md {

struct SettingsModel {
    Settings dock;
    MenuBarSettings bar;
};

// Fusion par différence : `file` reçoit les clés de `after` dont la valeur (sérialisée) diffère de `before`, et perd
// celles que `after` n'a plus (« Écran principal » retire screen) ; ses autres clés (inconnues de nous, épingles du
// Dock) restent telles quelles.
json::Value mergeChanged(json::Value file, const json::Value& before, const json::Value& after);

// État des fichiers lus : invalide (JSON cassé) ou illisible (verrou…). Un tel fichier n'est jamais réécrit : le Dock
// garde alors ses réglages actuels, et l'app ne doit pas le remplacer par un fichier presque vide.
struct ModelFiles {
    bool dockInvalid = false, barInvalid = false;
};

SettingsModel loadModel(const std::wstring& dir, ModelFiles* status = nullptr);   // absents : réglages par défaut

// Recharge les fichiers de `dir`, applique `edit`, puis écrit (atomiquement) seulement ceux qui changent. false si une
// écriture échoue ou si un fichier à écrire est invalide ou illisible ; `result` reçoit le modèle obtenu.
bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result = nullptr);

} // namespace md
