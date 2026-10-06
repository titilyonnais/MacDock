// Lecture et écriture des fichiers JSON de configuration.
#pragma once
#include <string>

#include "../core/json.h"

namespace md {

std::wstring appDataDir();   // %APPDATA%\MacDock, créé si absent

struct LoadResult {
    json::Value value;       // toujours un objet
    bool fromFile = false;   // le fichier existait et était valide
    bool wasInvalid = false; // le fichier existait mais était illisible (sauvegardé en .bak)
};

LoadResult loadJsonFile(const std::wstring& path);
bool saveJsonFileAtomic(const std::wstring& path, const json::Value& v);

} // namespace md
