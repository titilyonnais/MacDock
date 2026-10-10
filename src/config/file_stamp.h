// Empreinte d'un fichier de réglages (relecture de la nuit, plan 69) : le dossier de MacDock est surveillé en entier, et
// la barre y réécrit ses récents à chaque changement d'app ; le Dock ne recharge que si ses propres fichiers ont changé.
#pragma once
#include <cstdint>
#include <string>

namespace md {

struct FileStamp {
    bool exists = false;
    std::uint64_t written = 0;   // dernière écriture (FILETIME)
    std::uint64_t size = 0;
    bool operator==(const FileStamp&) const = default;
};

FileStamp fileStamp(const std::wstring& path);

} // namespace md
