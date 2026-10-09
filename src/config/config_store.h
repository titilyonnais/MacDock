// Lecture et écriture des fichiers JSON de configuration.
#pragma once
#include <string>

#include "../core/json.h"
#include "settings.h"

namespace md {

std::wstring appDataDir();   // %APPDATA%\MacDock, créé si absent

struct LoadResult {
    json::Value value;       // toujours un objet
    bool fromFile = false;   // le fichier existait et était valide
    bool wasInvalid = false; // le fichier existait mais son JSON était invalide (sauvegardé en .bak)
    bool unreadable = false; // le fichier existe mais n'a pas pu être lu (verrou, taille…)
};

LoadResult loadJsonFile(const std::wstring& path);
bool saveJsonFileAtomic(const std::wstring& path, const json::Value& v);

// Les épingles par défaut ne s'importent que pour un fichier absent ou jamais initialisé, jamais
// pour un fichier invalide ou illisible (on écraserait les épingles de l'utilisateur).
bool shouldImportDefaultPins(const LoadResult& file, const Settings& parsed);

// Fusion par différence : `file` reçoit les clés de `after` dont la valeur (sérialisée) diffère de `before`, et perd
// celles que `after` n'a plus (« Écran principal » retire screen) ; ses autres clés (inconnues de nous, épingles du
// Dock) restent telles quelles.
json::Value mergeChanged(json::Value file, const json::Value& before, const json::Value& after);
// Ce que le Dock écrit dans settings.json : seulement ce qu'il a changé depuis sa dernière lecture ou écriture
// (`lastSaved`), sur le fichier relu juste avant. Une clé écrite entre-temps par l'app Réglages, avant que le Dock
// relise le fichier, reste donc. Fichier absent, invalide ou illisible : tout.
json::Value dockSettingsToWrite(const LoadResult& file, const json::Value& lastSaved, const json::Value& now);

} // namespace md
