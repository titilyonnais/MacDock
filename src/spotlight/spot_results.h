// Résultats de Spotlight (logique pure) : sections, adresse de recherche Windows, raccourci clavier.
#pragma once
#include <windows.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "../apps/app_catalog.h"

namespace md {

enum class SpotKind { Calc, App, File };

struct SpotItem {
    SpotKind kind = SpotKind::App;
    std::wstring title, subtitle;
    std::wstring target;   // App : shell:AppsFolder\… ; File : chemin ; Calc : résultat à copier
};

struct SpotSection {
    std::wstring title;
    std::vector<SpotItem> items;
};

// Meilleur résultat (le calcul, sinon la première app), Applications (6 au plus), Documents (8 au plus) ;
// sections vides omises ; requête vide : aucune.
std::vector<SpotSection> spotlightResults(const std::wstring& query, const std::vector<AppEntry>& apps,
                                          const std::vector<SpotItem>& files);
std::size_t spotCount(const std::vector<SpotSection>& sections);
const SpotItem* spotAt(const std::vector<SpotSection>& sections, std::size_t index);   // nullptr hors limites
// Au plus maxRows lignes, dans l'ordre ; les sections vidées disparaissent (panneau sur un petit écran).
std::vector<SpotSection> spotTrim(std::vector<SpotSection> sections, std::size_t maxRows);

// Recherche de documents utile : requête non vide qui n'est pas un calcul.
bool wantsFileSearch(const std::wstring& query);
// Emplacement court d'un document : les deux derniers dossiers, « Documents › Factures ».
std::wstring shortFolder(const std::wstring& folder);

// Dossier de recherche du Shell (index de Windows) sur folder : search-ms:query=…&crumb=location:…
std::wstring searchMsUrl(const std::wstring& query, const std::wstring& folder);

struct HotkeySpec {
    UINT mods = 0;
    UINT vk = 0;
};
// « alt+space » ou « ctrl+space » (casse ignorée) ; nullopt pour « off » ou une valeur inconnue.
std::optional<HotkeySpec> parseSpotlightHotkey(const std::wstring& text);

} // namespace md
