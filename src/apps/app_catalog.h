// Catalogue de l'écran Apps : apps du menu Démarrer (dossier Shell « Apps »), filtrées, triées, recherchées.
// Logique pure : la lecture du dossier est dans apps_folder.h.
#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace md {

struct AppEntry {
    std::wstring name;          // nom affiché
    std::wstring parsingName;   // AUMID ou chemin, dans le dossier Apps
};

std::wstring launchTarget(const AppEntry& e);   // shell:AppsFolder\<parsingName>
// Une app à montrer : ni désinstalleur, ni aide, document ou lien web.
bool isListedApp(const AppEntry& e);
// Filtrées, sans doublon (même nom d'analyse), triées par nom (casse et accents ignorés, chiffres comme nombres).
std::vector<AppEntry> catalogFrom(std::vector<AppEntry> raw);
// Minuscules, accents retirés : la forme comparée par la recherche.
std::wstring foldForSearch(std::wstring_view text);
// Indices dans l'ordre d'affichage : début du nom, puis début d'un mot, puis ailleurs ; requête vide : tout.
std::vector<std::size_t> searchApps(const std::vector<AppEntry>& apps, const std::wstring& query);

} // namespace md
