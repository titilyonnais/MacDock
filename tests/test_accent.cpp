// Couleur d'accentuation (plan 57) : les huit accents de Réglages Système > Apparence, teintes claires et sombres.
#include <string>

#include "minitest.h"
#include "../src/core/accent.h"

TEST_CASE(accent_table_like_macos) {
    const auto& list = md::accentList();
    REQUIRE(list.size() == 8);
    const char* keys[] = {"blue", "purple", "pink", "red", "orange", "yellow", "green", "graphite"};
    const wchar_t* names[] = {L"Bleu", L"Violet", L"Rose", L"Rouge", L"Orange", L"Jaune", L"Vert", L"Graphite"};
    for (std::size_t i = 0; i < list.size(); ++i) {   // l'ordre de macOS
        CHECK(std::string(list[i].key) == keys[i]);
        CHECK(std::wstring(list[i].name) == names[i]);
        CHECK(int(list[i].id) == int(i));
    }
    // Teintes de macOS 26 (claire, sombre), celles des tuiles de Réglages et du rouge d'alerte.
    CHECK(md::accentRgb(md::AccentColor::Blue, false) == 0x0088FFu);
    CHECK(md::accentRgb(md::AccentColor::Blue, true) == 0x0091FFu);
    CHECK(md::accentRgb(md::AccentColor::Purple, false) == 0xCB30E0u);
    CHECK(md::accentRgb(md::AccentColor::Purple, true) == 0xDB34F2u);
    CHECK(md::accentRgb(md::AccentColor::Pink, false) == 0xFF2D55u);
    CHECK(md::accentRgb(md::AccentColor::Red, true) == 0xFF4245u);
    CHECK(md::accentRgb(md::AccentColor::Orange, false) == 0xFF8D28u);
    CHECK(md::accentRgb(md::AccentColor::Yellow, true) == 0xFFD600u);
    CHECK(md::accentRgb(md::AccentColor::Green, false) == 0x34C759u);
    CHECK(md::accentRgb(md::AccentColor::Graphite, true) == 0x98989Du);
}

TEST_CASE(accent_keys_roundtrip) {
    for (const auto& a : md::accentList()) {
        const auto back = md::accentFromKey(a.key);
        REQUIRE(back.has_value());
        CHECK(*back == a.id);
        CHECK(std::string(md::accentKey(a.id)) == a.key);
        CHECK(std::wstring(md::accentName(a.id)) == a.name);
    }
    CHECK(!md::accentFromKey("violet"));   // clés anglaises, comme les autres réglages
    CHECK(!md::accentFromKey(""));
    CHECK(!md::accentFromKey("Blue"));
}

TEST_CASE(accent_text_stays_readable) {   // blanc sur l'accent, noir sur le jaune
    for (const auto& a : md::accentList())
        for (const bool dark : {false, true}) {
            const std::uint32_t ink = md::onAccentRgb(a.id, dark);
            CHECK(ink == (a.id == md::AccentColor::Yellow ? 0x000000u : 0xFFFFFFu));
        }
}
