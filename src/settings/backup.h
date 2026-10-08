// Sauvegarde des réglages de MacDock : les deux fichiers (settings.json, menubar.json) dans un seul document, à
// exporter, importer, ou à remettre aux valeurs par défaut (apps épinglées gardées).
#pragma once
#include <optional>
#include <string>

#include "../core/json.h"

namespace md {

// {"macdockBackup": 1, "settings": {…}, "menubar": {…}} : les fichiers tels qu'ils sont (épingles comprises) ; un
// fichier absent n'y est pas. nullopt si un fichier présent est invalide ou illisible : la sauvegarde serait vide et,
// réimportée, effacerait tout.
std::optional<json::Value> exportSettings(const std::wstring& dir);
// Fichier à importer, lu sans rien écrire à côté de lui (pas de .bak dans le dossier de l'utilisateur) ; nullopt s'il
// est illisible, trop grand ou n'est pas un objet JSON.
std::optional<json::Value> readSettingsBackup(const std::wstring& path);
// Épingles de la sauvegarde qui pointent vers le réseau (\\serveur\…) : le Dock les contacterait dès son démarrage.
int networkPins(const json::Value& backup);
bool isSettingsBackup(const json::Value& v);

enum class ImportResult { Ok, NotABackup, Invalid, WriteFailed };
// Remplace les fichiers présents dans la sauvegarde (chacun doit être un objet) ; rien n'est écrit si elle est invalide.
ImportResult importSettings(const std::wstring& dir, const json::Value& backup);

// Préférences du Dock et de la barre remises aux défauts ; les apps épinglées (et leurs piles) restent.
bool resetSettings(const std::wstring& dir);

}  // namespace md
