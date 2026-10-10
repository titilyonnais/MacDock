// Cache des icônes du Dock sur disque (plan 68) : les images déjà mises en forme, relues au démarrage pour que le Dock
// soit complet dès ses premières images. Cache de la machine (%LOCALAPPDATA%), lu et écrit sur le fil des icônes
// seulement, jamais sur celui du Dock.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "icon_provider.h"

namespace md {

// Empreinte des réglages des icônes : un réglage changé donne une autre entrée.
std::uint64_t iconStyleFingerprint(const IconStyle& style);
// Clé d'une entrée : clé de l'image dans le cache du Dock (taille comprise) et empreinte des réglages.
std::wstring iconDiskKey(const std::wstring& cacheKey, const IconStyle& style);
// Place d'un travail sur disque : sa clé de cache, sauf pour un aperçu de pile, rangé à une seule place par pile et par
// taille (sa clé de cache change avec chaque fichier ajouté : un fichier par renouvellement remplirait la borne).
std::wstring iconDiskSlot(const IconJob& job);

// Fichier : en-tête (signature, version, taille, longueur de la clé), clé, pixels BGRA prémultipliés.
std::vector<std::uint8_t> encodeIconFile(const std::wstring& diskKey, const IconProvider::Image& image);
// nullptr si le fichier est abîmé, tronqué, d'une autre version ou d'une autre clé (collision de nom).
IconProvider::ImagePtr decodeIconFile(const std::vector<std::uint8_t>& bytes, const std::wstring& diskKey);
bool sameIcon(const IconProvider::Image& a, const IconProvider::Image& b);

class IconDiskCache {
public:
    explicit IconDiskCache(std::wstring dir, std::size_t maxEntries = 300, std::uint64_t maxBytes = 20ull << 20);
    // nullptr : absente, illisible ou abîmée.
    IconProvider::ImagePtr read(const std::wstring& diskKey) const;
    // Fichier temporaire, puis renommage : un arrêt pendant l'écriture laisse l'ancienne entrée ou la nouvelle, entière.
    bool write(const std::wstring& diskKey, const IconProvider::Image& image);
    // Entrée encore utilisée et inchangée : date d'écriture mise à jour, pour rester parmi les récentes.
    void touch(const std::wstring& diskKey);
    void remove(const std::wstring& diskKey);   // entrée périmée (préparation ratée)
    // Au-delà de maxEntries entrées ou de maxBytes octets, les plus anciennes (date d'écriture) sont effacées, ainsi
    // que les fichiers temporaires d'une écriture interrompue. Les autres fichiers du dossier ne sont jamais touchés.
    void trim();
    std::wstring pathFor(const std::wstring& diskKey) const;

private:
    std::wstring dir_;
    std::size_t maxEntries_;
    std::uint64_t maxBytes_;
};

} // namespace md
