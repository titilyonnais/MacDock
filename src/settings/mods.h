// Mods Windhawk de MacDock dans l'app Réglages : version de la source livrée, version installée, état.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace md {

struct ModInfo {
    std::wstring id;       // « macdock-look » (fichier windhawk\<id>.wh.cpp, clé local@<id>)
    std::wstring title;    // nom affiché
    std::wstring detail;   // une ligne d'explication
    std::wstring script;   // installateur PowerShell (windhawk\…)
};
const std::vector<ModInfo>& macdockMods();

// « @version 1.2.0 » de l'en-tête du mod ; nullopt sans version.
std::optional<std::wstring> sourceVersion(std::string_view source);
// Comparaison de versions « 1.10.0 » (champs manquants = 0) : négatif, zéro ou positif.
int compareVersions(std::wstring_view a, std::wstring_view b);
// Source d'un mod : à côté de l'exécutable (windhawk\<id>.wh.cpp), puis dans le dépôt (build\Release → ..\..\windhawk).
std::wstring modSourcePath(const std::wstring& exeDir, const std::wstring& id);
std::optional<std::wstring> modSourceVersion(const std::wstring& path);   // lit le fichier

struct InstalledMod {
    std::wstring version;
    bool disabled = false;
};
bool windhawkInstalled();                                            // HKLM\SOFTWARE\Windhawk
std::optional<InstalledMod> installedMod(const std::wstring& id);    // HKLM\SOFTWARE\Windhawk\Engine\Mods\local@<id>

enum class ModStatus { WindhawkMissing, NotInstalled, UpToDate, UpdateAvailable, Disabled };
ModStatus modStatus(bool windhawk, const std::optional<InstalledMod>& installed, const std::optional<std::wstring>& available);

}  // namespace md
