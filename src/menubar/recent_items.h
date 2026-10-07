// Éléments récents du menu du système : dernières apps passées au premier plan (gardées par la barre) et derniers
// documents du dossier Récents de Windows (lu, jamais modifié).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "../core/json.h"
#include "../icons/icon_provider.h"

namespace md {

struct RecentEntry {
    std::wstring name, target;     // target : cible de relance (app) ou raccourci du dossier Récents (document)
    IconProvider::ImagePtr icon;   // facultative, ajoutée par la barre
};

struct RecentState {
    std::vector<RecentEntry> apps;   // la plus récente en tête
    std::uint64_t clearedAt = 0;     // FILETIME de « Effacer le menu » : documents plus anciens masqués
};

constexpr std::size_t kRecentMax = 10;

// En tête, sans doublon (cible, casse ignorée), max au plus ; une entrée sans cible est ignorée.
void pushRecent(std::vector<RecentEntry>& list, RecentEntry e, std::size_t max = kRecentMax);
// Raccourcis de documents du dossier, du plus récent au plus ancien, modifiés après clearedAt. Les raccourcis
// sans extension de document (dossiers : « Téléchargements.lnk ») sont écartés.
std::vector<RecentEntry> recentDocuments(const std::wstring& folder, std::size_t max, std::uint64_t clearedAt);

RecentState recentFromJson(const json::Value& v);
json::Value recentToJson(const RecentState& s);

} // namespace md
