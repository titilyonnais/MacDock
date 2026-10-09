// Couleur d'accentuation de MacDock (logique pure), comme Réglages Système > Apparence sur macOS 26 : huit accents,
// teintes claire et sombre, texte lisible dessus. Plan 57.
#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace md {

enum class AccentColor { Blue, Purple, Pink, Red, Orange, Yellow, Green, Graphite };

struct AccentInfo {
    AccentColor id;
    const char* key;             // settings.json
    const wchar_t* name;         // Réglages
    std::uint32_t light, dark;   // 0xRRGGBB
};

// Ordre et teintes de macOS 26 / iOS 26 (celles des tuiles de Réglages et du rouge d'alerte du projet) ; les valeurs
// exactes de l'accent de macOS ne sont pas publiées : à recaler sur une VM macOS 26.
inline const std::array<AccentInfo, 8>& accentList() {
    static const std::array<AccentInfo, 8> list{{
        {AccentColor::Blue, "blue", L"Bleu", 0x0088FF, 0x0091FF},
        {AccentColor::Purple, "purple", L"Violet", 0xCB30E0, 0xDB34F2},
        {AccentColor::Pink, "pink", L"Rose", 0xFF2D55, 0xFF375F},
        {AccentColor::Red, "red", L"Rouge", 0xFF383C, 0xFF4245},
        {AccentColor::Orange, "orange", L"Orange", 0xFF8D28, 0xFF9230},
        {AccentColor::Yellow, "yellow", L"Jaune", 0xFFCC00, 0xFFD600},
        {AccentColor::Green, "green", L"Vert", 0x34C759, 0x30D158},
        {AccentColor::Graphite, "graphite", L"Graphite", 0x8E8E93, 0x98989D},
    }};
    return list;
}

inline const AccentInfo& accentInfo(AccentColor a) {
    const auto& list = accentList();
    const auto i = std::size_t(a);
    return i < list.size() ? list[i] : list[0];
}
inline std::optional<AccentColor> accentFromKey(std::string_view key) {
    for (const AccentInfo& a : accentList())
        if (key == a.key) return a.id;
    return std::nullopt;
}
inline const char* accentKey(AccentColor a) { return accentInfo(a).key; }
inline const wchar_t* accentName(AccentColor a) { return accentInfo(a).name; }
inline std::uint32_t accentRgb(AccentColor a, bool dark) { return dark ? accentInfo(a).dark : accentInfo(a).light; }

// Texte et coches sur l'accent : blanc, ou noir quand l'accent est trop clair (jaune) ; luminance relative de sRGB.
inline std::uint32_t onAccentRgb(AccentColor a, bool dark) {
    const std::uint32_t c = accentRgb(a, dark);
    auto lin = [](std::uint32_t v) {
        const double s = double(v) / 255.0;
        return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
    };
    const double l = 0.2126 * lin((c >> 16) & 255) + 0.7152 * lin((c >> 8) & 255) + 0.0722 * lin(c & 255);
    return l > 0.5 ? 0x000000 : 0xFFFFFF;
}

} // namespace md
