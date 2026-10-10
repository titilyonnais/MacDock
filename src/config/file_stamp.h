// Empreinte d'un fichier de réglages (relecture de la nuit, plan 69) : le dossier de MacDock est surveillé en entier, et
// la barre y réécrit ses récents à chaque changement d'app ; le Dock ne recharge que si ses propres fichiers ont changé.
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace md {

struct FileStamp {
    bool exists = false;
    std::uint64_t written = 0;   // dernière écriture (FILETIME)
    std::uint64_t size = 0;
    bool operator==(const FileStamp&) const = default;
};

FileStamp fileStamp(const std::wstring& path);

// Fichiers de réglages surveillés : leurs empreintes sont prises AVANT de les lire, si bien qu'une écriture tombée pendant
// la lecture rend l'empreinte différente et que la relecture suivante la prend (relecture du plan 69 : prises après, une
// écriture de l'app Réglages entre les deux restait ignorée).
class ConfigWatch {
public:
    ConfigWatch() = default;
    explicit ConfigWatch(std::vector<std::wstring> files) : files_(std::move(files)) {}
    template <class Read> void read(Read&& body) {
        std::vector<FileStamp> before = stamps();
        body();
        last_ = std::move(before);
        read_ = true;
    }
    bool changed() const { return !read_ || stamps() != last_; }   // jamais lu, ou un fichier a changé depuis

private:
    std::vector<FileStamp> stamps() const;
    std::vector<std::wstring> files_;
    std::vector<FileStamp> last_;
    bool read_ = false;
};

} // namespace md
