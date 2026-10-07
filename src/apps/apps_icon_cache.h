// Icônes de l'écran Apps gardées d'une ouverture à l'autre (partagé avec le fil de chargement).
#pragma once
#include <map>
#include <mutex>
#include <string>
#include <utility>

#include "../icons/icon_provider.h"

namespace md {

class AppsIconCache {
public:
    // Signature des réglages d'icônes (mode sombre, grille, dossier personnalisé) : un changement vide le cache.
    void setStyle(const std::wstring& signature);
    IconProvider::ImagePtr find(const std::wstring& key, int px);
    void put(const std::wstring& key, int px, IconProvider::ImagePtr image);

private:
    std::mutex mutex_;
    std::wstring style_;
    std::map<std::pair<std::wstring, int>, IconProvider::ImagePtr> images_;
};

} // namespace md
