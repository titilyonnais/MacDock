// Version de MacDock : celle de version_defs.h, affichée dans l'app Réglages (À propos) et comparée à la dernière
// publication sur GitHub (mises à jour, plan 53).
#pragma once
#include <optional>
#include <string>
#include <string_view>

#include "version_defs.h"

namespace md {

inline constexpr wchar_t kMacDockVersion[] = L"" MACDOCK_VERSION_STRING;

struct Version {
    int major = 0, minor = 0, patch = 0;
    std::wstring pre;   // préversion (« rc.1 ») ; vide pour une version publiée
    bool operator==(const Version&) const = default;
};

// « 0.52.0 », « v0.52.0 » (étiquette de GitHub), « 0.52 » (correctif 0), « 0.53.0-rc.1 », métadonnées « +… »
// ignorées ; nullopt pour tout le reste (texte, espaces, nombres négatifs ou trop grands, champs vides).
std::optional<Version> parseVersion(std::wstring_view text);
// < 0, 0 ou > 0, comme semver : majeur, mineur, correctif ; une préversion passe avant sa version ; deux préversions se
// comparent champ par champ (nombres entre eux, un nombre avant du texte, moins de champs avant).
int compareVersions(const Version& a, const Version& b);
std::wstring versionText(const Version& v);   // « 0.53.0-rc.1 »

} // namespace md
