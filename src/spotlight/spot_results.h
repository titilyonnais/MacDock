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

// Clic sur le Dock (qui n'active jamais) pendant une fenêtre modale : il ferme Spotlight, comme un clic ailleurs ;
// pendant un menu, une pile ou l'écran Apps, il est ignoré (pas de fenêtre modale dans une autre).
enum class DockClick { Proceed, CloseSpotlight, Ignore };
DockClick dockClickGate(bool spotlightOpen, bool modalOpen);

// Documents du panneau : les anciens restent affichés pendant la frappe, jusqu'à la réponse à la dernière
// demande ; une réponse à une demande dépassée est ignorée.
struct DocFeed {
    std::vector<SpotItem> shown;
    unsigned awaited = 0;   // génération attendue (FileSearcher::request) ; 0 : aucune
    void typed(bool searching) {   // nouvelle frappe ; searching faux : calcul ou champ vide
        awaited = 0;
        if (!searching) shown.clear();
    }
    void asked(unsigned generation) { awaited = generation; }
    bool arrived(unsigned generation, std::vector<SpotItem> items) {
        if (!awaited || generation != awaited) return false;
        shown = std::move(items);
        return true;
    }
};

struct HotkeySpec {
    UINT mods = 0;
    UINT vk = 0;
};
// « alt+space » ou « ctrl+space » (casse ignorée) ; nullopt pour « off » ou une valeur inconnue.
std::optional<HotkeySpec> parseSpotlightHotkey(const std::wstring& text);

// Saisie : efface le dernier caractère (une paire de substitution, comme un émoji, d'un coup).
void spotEraseLast(std::wstring& query);
// Collage : première ligne, tabulations en espaces, 128 unités au plus sans couper une paire de substitution.
std::wstring spotPasteLine(const std::wstring& clip);
// Coupe text à max unités sans laisser de moitié haute de paire seule à la fin.
void spotClip(std::wstring& text, std::size_t max);
// Frappe : c peut-il s'ajouter à query (128 unités au plus, pas de caractère de contrôle, paires entières) ?
bool spotAcceptChar(const std::wstring& query, wchar_t c);

} // namespace md
