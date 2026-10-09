// Spotlight : conversions d'unités (plan 55), « 10 km » → 6,21371 mi, comme la calculatrice.
#include <algorithm>
#include <cmath>
#include <string>

#include "minitest.h"
#include "../src/core/strings.h"
#include "../src/spotlight/spot_calc.h"
#include "../src/spotlight/spot_convert.h"

namespace {

// Conversion attendue : valeur à un millionième près, symboles affichés exacts ; le détail en cas d'échec.
void checkConv(const wchar_t* query, double value, const wchar_t* from, const wchar_t* to, int line) {
    const auto c = md::convertUnits(query);
    const std::string q = md::toUtf8(query);
    if (!c) {
        minitest::fail(__FILE__, line, "pas de conversion pour « " + q + " »");
        return;
    }
    const double tol = std::max(1e-9, std::abs(value) * 1e-6);
    if (std::abs(c->value - value) > tol || c->from != from || c->to != to)
        minitest::fail(__FILE__, line,
                       "« " + q + " » : " + std::to_string(c->value) + " " + md::toUtf8(c->to) + " depuis " +
                           md::toUtf8(c->from) + ", attendu " + std::to_string(value) + " " + md::toUtf8(to) +
                           " depuis " + md::toUtf8(from));
}

#define CONV(q, v, f, t) checkConv(q, v, f, t, __LINE__)

} // namespace

TEST_CASE(spot_format_significant_digits) {
    CHECK(md::formatNumber(6.21371192237, 6) == L"6,21371");
    CHECK(md::formatNumber(37.7777777778, 6) == L"37,7778");
    CHECK(md::formatNumber(2204.62262185, 6) == L"2 204,62");
    CHECK(md::formatNumber(0.000123456789, 6) == L"0,000123457");
    CHECK(md::formatNumber(68.0000000001, 6) == L"68");
    CHECK(md::formatNumber(-40, 6) == L"-40");
    CHECK(md::formatNumber(1e20, 6) == L"1e+20");
    CHECK(md::formatNumber(1.0 / 3) == L"0,3333333333");   // 10 chiffres par défaut, comme avant
}

TEST_CASE(spot_convert_default_targets) {   // vers l'unité usuelle de l'autre système
    CONV(L"10 km", 6.213711922, L"km", L"mi");
    CONV(L"10 mi", 16.09344, L"mi", L"km");
    CONV(L"2 m", 6.561679790, L"m", L"ft");
    CONV(L"5 kg", 11.02311311, L"kg", L"lb");
    CONV(L"8 oz", 226.7961850, L"oz", L"g");
    CONV(L"100 °F", 37.77777778, L"°F", L"°C");
    CONV(L"20 °C", 68, L"°C", L"°F");
    CONV(L"3 l", 0.7925161570, L"l", L"gal");
    CONV(L"1 m³", 1000, L"m³", L"l");
    CONV(L"100 km/h", 62.13711922, L"km/h", L"mph");
    CONV(L"20 kn", 37.04, L"kn", L"km/h");
    CONV(L"1 ha", 2.471053815, L"ha", L"ac");
    CONV(L"50 m²", 538.1955208, L"m²", L"ft²");
    CONV(L"500 Go", 465.6612873, L"Go", L"Gio");
    CONV(L"1 Gio", 1.073741824, L"Gio", L"Go");
    CONV(L"100 kcal", 418.4, L"kcal", L"kJ");
    CONV(L"2,5 bar", 36.25943443, L"bar", L"psi");
    CONV(L"1013 hPa", 759.8123763, L"hPa", L"mmHg");
}

TEST_CASE(spot_convert_explicit_targets) {
    CONV(L"10 km en miles", 6.213711922, L"km", L"mi");
    CONV(L"10 km in m", 10000, L"km", L"m");
    CONV(L"12 in to cm", 30.48, L"in", L"cm");
    CONV(L"12 in in cm", 30.48, L"in", L"cm");   // « in » : l'unité, puis le mot de liaison
    CONV(L"5 cm in in", 1.968503937, L"cm", L"in");
    CONV(L"1 Go vers Mo", 1000, L"Go", L"Mo");
    CONV(L"98,6 F → C", 37, L"°F", L"°C");
    CONV(L"98,6F→C", 37, L"°F", L"°C");
    CONV(L"1 h -> s", 3600, L"h", L"s");
    CONV(L"3 pieds en mètres", 0.9144, L"ft", L"m");
    CONV(L"1 tasse en ml", 236.5882365, L"cup", L"ml");
    CONV(L"16 fl oz en cl", 47.31764730, L"fl oz", L"cl");
    CONV(L"1 mille marin en km", 1.852, L"nmi", L"km");
    CONV(L"10 km en km", 10, L"km", L"km");
}

TEST_CASE(spot_convert_numbers_and_signs) {
    CONV(L"-40 °C", -40, L"°C", L"°F");
    CONV(L"−40 °F", -40, L"°F", L"°C");   // signe moins typographique
    CONV(L"1 500 m", 4921.259843, L"m", L"ft");
    CONV(L"1 500 m", 4921.259843, L"m", L"ft");   // espace fine des milliers, comme formatNumber
    CONV(L"2.5 kg", 5.511556555, L"kg", L"lb");
    CONV(L"10km", 6.213711922, L"km", L"mi");
    CONV(L"  10   km  ", 6.213711922, L"km", L"mi");
    const auto c = md::convertUnits(L"1 000,5 km");
    REQUIRE(c.has_value());
    CHECK(c->amount == 1000.5);
}

TEST_CASE(spot_convert_names_and_case) {
    CONV(L"10 kilomètres", 6.213711922, L"km", L"mi");
    CONV(L"10 KILOMETRES", 6.213711922, L"km", L"mi");   // les noms : sans casse ni accents
    CONV(L"3 pouces", 7.62, L"in", L"cm");
    CONV(L"20 nœuds", 37.04, L"kn", L"km/h");
    CONV(L"20 noeuds", 37.04, L"kn", L"km/h");
    CONV(L"300 kelvins", 26.85, L"K", L"°C");
    CONV(L"4 K", -269.15, L"K", L"°C");
    CONV(L"25 degrés Celsius", 77, L"°C", L"°F");
    CONV(L"8 bits", 1, L"bit", L"o");
    CONV(L"1 B", 8, L"o", L"bit");
    CONV(L"1 GB", 0.9313225746, L"Go", L"Gio");
    CHECK(!md::convertUnits(L"4K"));       // une vidéo 4K, pas des kelvins : le symbole seul demande une espace
    CHECK(!md::convertUnits(L"4k"));       // les symboles : casse exacte
    CHECK(!md::convertUnits(L"3d"));       // « 3D Builder » : jamais « d » pour jour
    CHECK(!md::convertUnits(L"1013 mb"));  // millibar ou mégaoctet ?
    CHECK(!md::convertUnits(L"2 pi"));     // π, pas des pieds
}

TEST_CASE(spot_convert_data_units) {
    CONV(L"1 Go en Mo", 1000, L"Go", L"Mo");
    CONV(L"1 GiB en MiB", 1024, L"Gio", L"Mio");
    CONV(L"2 To", 1.818989404, L"To", L"Tio");
    CONV(L"1 Mio en Ko", 1048.576, L"Mio", L"Ko");
    CONV(L"1 Go to Mo", 1000, L"Go", L"Mo");   // « to » : le mot de liaison, entre deux unités
    CONV(L"1 to", 0.9094947018, L"To", L"Tio");   // seul : des téraoctets
}

TEST_CASE(spot_convert_time_ladder) {   // la plus grande unité où le nombre vaut au moins 1
    CONV(L"90 min", 1.5, L"min", L"h");
    CONV(L"48 h", 2, L"h", L"j");
    CONV(L"2 h", 120, L"h", L"min");
    CONV(L"30 s", 0.5, L"s", L"min");   // rien au-dessus de 1 : l'unité juste au-dessus
    CONV(L"3600 s", 1, L"s", L"h");
    CONV(L"2 j", 48, L"j", L"h");
    CONV(L"14 jours", 2, L"j", L"sem");
    CONV(L"1500 ms", 1.5, L"ms", L"s");
    CONV(L"500 ms", 0.5, L"ms", L"s");
    CONV(L"-90 min", -1.5, L"min", L"h");
}

TEST_CASE(spot_convert_refuses) {
    CHECK(!md::convertUnits(L""));
    CHECK(!md::convertUnits(L"km"));            // pas de nombre
    CHECK(!md::convertUnits(L"10"));            // pas d'unité
    CHECK(!md::convertUnits(L"10 km en kg"));   // catégories différentes
    CHECK(!md::convertUnits(L"10 km en"));      // unité d'arrivée manquante
    CHECK(!md::convertUnits(L"10 zorglubs"));
    CHECK(!md::convertUnits(L"7 zip"));
    CHECK(!md::convertUnits(L"2+3 km"));        // un calcul, pas une conversion
    CHECK(!md::convertUnits(L"1,2,3 m"));
    CHECK(!md::convertUnits(L"10 km 5"));
    CHECK(!md::convertUnits(L"5h30"));          // une heure, pas une durée à convertir
    CHECK(!md::convertUnits(std::wstring(400, L'9') + L" km"));   // nombre infini
}
