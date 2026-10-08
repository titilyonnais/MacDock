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

// Fusion par différence : `file` reçoit les clés de `after` dont la valeur (sérialisée) diffère de `before` ; ses
// autres clés (inconnues de nous, épingles du Dock) restent telles quelles.
json::Value mergeChanged(json::Value file, const json::Value& before, const json::Value& after);

SettingsModel loadModel(const std::wstring& dir);   // fichiers absents ou invalides : réglages par défaut

// Recharge les fichiers de `dir`, applique `edit`, puis écrit (atomiquement) seulement ceux qui changent. false si une
// écriture échoue ; `result` reçoit le modèle obtenu.
bool commit(const std::wstring& dir, const std::function<void(SettingsModel&)>& edit, SettingsModel* result = nullptr);

} // namespace md
