// Sauvegarde des réglages de MacDock : les deux fichiers (settings.json, menubar.json) dans un seul document, à
// exporter, importer, ou à remettre aux valeurs par défaut (apps épinglées gardées).
#pragma once
#include <string>

#include "../core/json.h"

namespace md {

// {"macdockBackup": 1, "settings": {…}, "menubar": {…}} : les fichiers tels qu'ils sont (épingles comprises).
json::Value exportSettings(const std::wstring& dir);
bool isSettingsBackup(const json::Value& v);

enum class ImportResult { Ok, NotABackup, Invalid, WriteFailed };
// Remplace les fichiers présents dans la sauvegarde (chacun doit être un objet) ; rien n'est écrit si elle est invalide.
ImportResult importSettings(const std::wstring& dir, const json::Value& backup);

// Préférences du Dock et de la barre remises aux défauts ; les apps épinglées (et leurs piles) restent.
bool resetSettings(const std::wstring& dir);

}  // namespace md
