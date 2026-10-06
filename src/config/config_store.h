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

} // namespace md
