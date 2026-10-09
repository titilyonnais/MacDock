// Mises à jour automatiques (plan 53), logique pure : réponses de l'API de GitHub, choix de la version à proposer,
// empreintes de SHA256SUMS.txt, état gardé dans update.json et décision au démarrage du lanceur.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../core/version.h"

namespace md {

struct ReleaseAsset {
    std::wstring name, url;   // url : browser_download_url
    std::uint64_t size = 0;
};

struct Release {
    std::wstring tag;
    Version version;
    bool prerelease = false, draft = false;
    std::vector<ReleaseAsset> assets;
};

// Réponse de releases (tableau) ou de releases/latest (objet) ; les étiquettes qui ne sont pas des versions sont
// écartées. Vide si le JSON est illisible ou si c'est une erreur de l'API (limite atteinte…).
std::vector<Release> parseReleases(std::string_view json);

// Version à proposer : la plus récente des versions publiées (préversions seulement si `prerelease`), plus récente
// que `current`, avec son installateur « MacDock-Setup-<version>.exe », « SHA256SUMS.txt » et sa signature
// « SHA256SUMS.txt.sig », tous en HTTPS. Une version sans signature n'est jamais proposée.
struct UpdateOffer {
    Version version;
    ReleaseAsset installer, sums, signature;
};
std::optional<UpdateOffer> pickUpdate(const std::vector<Release>& releases, const Version& current, bool prerelease);
// Liste des versions publiées (et non releases/latest) : une version tout juste publiée, pas encore signée, ne cache
// pas la précédente, signée. Sans jeton, l'API ne montre jamais les brouillons.
std::wstring releasesUrl(const std::wstring& repo);
std::wstring installerName(const Version& v);   // « MacDock-Setup-0.53.0.exe »

// Empreinte d'un fichier dans SHA256SUMS.txt (lignes « <64 chiffres hexadécimaux>  <nom> », « *<nom> » accepté),
// en minuscules ; nullopt si absente ou mal formée.
std::optional<std::string> expectedSha256(std::string_view sums, std::wstring_view fileName);

// État gardé dans %APPDATA%\MacDock\update.json (lanceur et app Réglages).
struct UpdateState {
    bool automatic = true;          // recherches de fond (« Rechercher automatiquement »)
    std::int64_t lastCheck = 0;     // dernière recherche, secondes depuis 1970 (UTC)
    std::wstring readyVersion;      // version téléchargée et vérifiée, prête à installer ("" : aucune)
    std::wstring readyPath;         // son installateur
    std::string readySha256;        // son empreinte, revérifiée avant de la lancer
    std::wstring attemptedVersion;  // installation déjà lancée pour cette version (une seule tentative)
    std::wstring lastError;         // dernière erreur (app Réglages)
};
UpdateState parseUpdateState(std::string_view json);   // illisible : valeurs par défaut
std::string updateStateJson(const UpdateState& s);

// Recherche due ? `interval` secondes après la précédente ; jamais faite ou horloge reculée : oui.
bool checkDue(std::int64_t lastCheck, std::int64_t now, std::int64_t interval = 12 * 3600);

// Au démarrage du lanceur, avant le Dock : installer la version prête (plus récente que `current`, installateur
// présent, pas encore tentée) ; oublier une tentative qui n'a pas pris (toujours l'ancienne version) ; oublier une
// version prête périmée (déjà installée, installateur disparu) ; ou rien.
enum class StartupAction { None, Install, DropAttempt, DropStale };
StartupAction startupAction(const UpdateState& s, const Version& current, bool installerPresent);

} // namespace md
