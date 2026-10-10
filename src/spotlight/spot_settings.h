// Réglages proposés par Spotlight (logique pure) : sections de Réglages MacDock et pages des Paramètres de Windows 11,
// comme les Réglages Système dans Spotlight sur macOS 26.
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace md {

struct SettingsPage {
    bool macDock = false;    // section de Réglages MacDock (cible : sa clé de --pane) ; sinon page de Windows
    std::wstring title;      // nom affiché
    std::wstring target;     // clé de --pane, ou adresse ms-settings:
    std::wstring keywords;   // mots cherchés en plus du nom
};

// Sections de Réglages MacDock (ordre de la barre latérale), puis pages des Paramètres de Windows 11.
const std::vector<SettingsPage>& settingsPages();
// Indices dans l'ordre d'affichage : début du nom, puis début d'un mot du nom, puis début d'un mot-clé ; à égalité,
// l'ordre de la table. Sans casse ni accents ; espaces et traits d'union interchangeables ou facultatifs (« wi fi »,
// « wifi ») ; dans les noms, singulier et pluriel confondus sauf au dernier mot tapé (« capture d'écran » :
// « Captures d'écran ») ; requête vide : aucun.
std::vector<std::size_t> searchSettings(const std::vector<SettingsPage>& pages, const std::wstring& query);
// Le nom de la page est exactement la requête (sans casse ni accents, aux espaces et traits d'union près) ; plural :
// aux pluriels près aussi (« capture d'écran » : « Captures d'écran »).
bool exactSettingTitle(const SettingsPage& page, const std::wstring& query, bool plural = false);
// La page est trouvée par son nom (début du nom ou d'un de ses mots), pas seulement par un mot-clé.
bool settingTitleMatch(const SettingsPage& page, const std::wstring& query);

} // namespace md
