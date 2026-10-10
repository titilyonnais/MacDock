#include "spot_convert.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "../apps/app_catalog.h"

namespace md {

namespace {

enum class Cat { Length, Mass, Temperature, Volume, Area, Speed, Time, Data, Energy, Pressure };

struct Unit {
    std::wstring id;                     // symbole affiché
    Cat cat;
    double factor, offset;               // dans l'unité de base de la catégorie : v × factor + offset
    std::wstring to;                     // unité d'arrivée par défaut ; vide : échelle des durées
    std::vector<std::wstring> symbols;   // casse exacte
    std::vector<std::wstring> spaced;    // casse exacte, seulement après une espace : « 4 K », jamais « 4K »
    std::vector<std::wstring> names;     // pliés (minuscules, sans accents, « œ » en « oe »)
};

// Unités de base : mètre, kilogramme, kelvin, litre, mètre carré, mètre par seconde, seconde, octet, joule, pascal.
// Volumes des États-Unis (gallon, quart, pinte, once liquide, tasse). « d », « pi », « mb », « nd » et « pt » n'y sont
// pas exprès ; les symboles d'une lettre ambigus (C, F, K, o, B) demandent une espace après le nombre.
const std::vector<Unit>& units() {
    static const std::vector<Unit> table = {
        {L"mm", Cat::Length, 1e-3, 0, L"in", {L"mm"}, {}, {L"millimetre", L"millimetres", L"millimeter", L"millimeters"}},
        {L"cm", Cat::Length, 1e-2, 0, L"in", {L"cm"}, {}, {L"centimetre", L"centimetres", L"centimeter", L"centimeters"}},
        {L"m", Cat::Length, 1, 0, L"ft", {L"m"}, {}, {L"metre", L"metres", L"meter", L"meters"}},
        {L"km", Cat::Length, 1e3, 0, L"mi", {L"km", L"Km", L"KM"}, {}, {L"kilometre", L"kilometres", L"kilometer", L"kilometers"}},
        {L"in", Cat::Length, 0.0254, 0, L"cm", {L"in", L"\"", L"″", L"po"}, {}, {L"pouce", L"pouces", L"inch", L"inches"}},
        {L"ft", Cat::Length, 0.3048, 0, L"m", {L"ft", L"'", L"′"}, {}, {L"pied", L"pieds", L"foot", L"feet"}},
        {L"yd", Cat::Length, 0.9144, 0, L"m", {L"yd"}, {}, {L"yard", L"yards", L"verge", L"verges"}},
        {L"mi", Cat::Length, 1609.344, 0, L"km", {L"mi"}, {}, {L"mile", L"miles"}},
        {L"nmi", Cat::Length, 1852, 0, L"km", {L"nmi", L"NM"}, {},
         {L"mille marin", L"milles marins", L"mille nautique", L"milles nautiques", L"nautical mile", L"nautical miles"}},

        {L"mg", Cat::Mass, 1e-6, 0, L"g", {L"mg"}, {}, {L"milligramme", L"milligrammes", L"milligram", L"milligrams"}},
        {L"g", Cat::Mass, 1e-3, 0, L"oz", {L"g"}, {}, {L"gramme", L"grammes", L"gram", L"grams"}},
        {L"kg", Cat::Mass, 1, 0, L"lb", {L"kg", L"Kg", L"KG"}, {},
         {L"kilogramme", L"kilogrammes", L"kilogram", L"kilograms", L"kilo", L"kilos"}},
        {L"t", Cat::Mass, 1e3, 0, L"lb", {L"t"}, {}, {L"tonne", L"tonnes"}},
        {L"oz", Cat::Mass, 0.028349523125, 0, L"g", {L"oz"}, {}, {L"once", L"onces", L"ounce", L"ounces"}},
        {L"lb", Cat::Mass, 0.45359237, 0, L"kg", {L"lb", L"lbs"}, {}, {L"livre", L"livres", L"pound", L"pounds"}},

        {L"°C", Cat::Temperature, 1, 273.15, L"°F", {L"°C", L"ºC", L"°c", L"℃", L"° C"}, {L"C"},
         {L"celsius", L"degre celsius", L"degres celsius"}},
        {L"°F", Cat::Temperature, 5.0 / 9, 273.15 - 32 * 5.0 / 9, L"°C", {L"°F", L"ºF", L"°f", L"℉", L"° F"}, {L"F"},
         {L"fahrenheit", L"degre fahrenheit", L"degres fahrenheit"}},
        {L"K", Cat::Temperature, 1, 0, L"°C", {}, {L"K"}, {L"kelvin", L"kelvins"}},

        {L"ml", Cat::Volume, 1e-3, 0, L"fl oz", {L"ml", L"mL"}, {}, {L"millilitre", L"millilitres", L"milliliter", L"milliliters"}},
        {L"cl", Cat::Volume, 1e-2, 0, L"fl oz", {L"cl", L"cL"}, {}, {L"centilitre", L"centilitres", L"centiliter", L"centiliters"}},
        {L"dl", Cat::Volume, 0.1, 0, L"fl oz", {L"dl", L"dL"}, {}, {L"decilitre", L"decilitres", L"deciliter", L"deciliters"}},
        {L"l", Cat::Volume, 1, 0, L"gal", {L"l", L"L"}, {}, {L"litre", L"litres", L"liter", L"liters"}},
        {L"m³", Cat::Volume, 1e3, 0, L"l", {L"m³", L"m3"}, {}, {L"metre cube", L"metres cubes", L"cubic meter", L"cubic meters"}},
        {L"gal", Cat::Volume, 3.785411784, 0, L"l", {L"gal"}, {}, {L"gallon", L"gallons"}},
        {L"qt", Cat::Volume, 0.946352946, 0, L"l", {L"qt"}, {}, {}},
        {L"pt", Cat::Volume, 0.473176473, 0, L"l", {}, {}, {L"pint", L"pints"}},   // « 12 pt » : un corps de police
        {L"fl oz", Cat::Volume, 0.0295735295625, 0, L"ml", {L"fl oz", L"fl. oz", L"floz"}, {},
         {L"once liquide", L"onces liquides", L"fluid ounce", L"fluid ounces"}},
        {L"cup", Cat::Volume, 0.2365882365, 0, L"ml", {L"cup", L"cups"}, {}, {L"tasse", L"tasses"}},

        {L"cm²", Cat::Area, 1e-4, 0, L"in²", {L"cm²", L"cm2"}, {}, {L"centimetre carre", L"centimetres carres"}},
        {L"m²", Cat::Area, 1, 0, L"ft²", {L"m²", L"m2"}, {},
         {L"metre carre", L"metres carres", L"square meter", L"square meters"}},
        {L"km²", Cat::Area, 1e6, 0, L"mi²", {L"km²", L"km2"}, {}, {L"kilometre carre", L"kilometres carres"}},
        {L"ha", Cat::Area, 1e4, 0, L"ac", {L"ha"}, {}, {L"hectare", L"hectares"}},
        {L"ac", Cat::Area, 4046.8564224, 0, L"ha", {L"ac"}, {}, {L"acre", L"acres"}},
        {L"in²", Cat::Area, 6.4516e-4, 0, L"cm²", {L"in²", L"in2", L"po²"}, {},
         {L"pouce carre", L"pouces carres", L"square inch", L"square inches"}},
        {L"ft²", Cat::Area, 0.09290304, 0, L"m²", {L"ft²", L"ft2", L"pi²"}, {},
         {L"pied carre", L"pieds carres", L"square foot", L"square feet"}},
        {L"mi²", Cat::Area, 2589988.110336, 0, L"km²", {L"mi²", L"mi2"}, {},
         {L"mile carre", L"miles carres", L"square mile", L"square miles"}},

        {L"m/s", Cat::Speed, 1, 0, L"km/h", {L"m/s"}, {}, {L"metre par seconde", L"metres par seconde"}},
        {L"km/h", Cat::Speed, 1 / 3.6, 0, L"mph", {L"km/h", L"kmh", L"kph", L"km/hr"}, {},
         {L"kilometre par heure", L"kilometres par heure", L"kilometre-heure", L"kilometres-heure", L"kilometres heure"}},
        {L"mph", Cat::Speed, 0.44704, 0, L"km/h", {L"mph", L"mi/h"}, {},
         {L"mile par heure", L"miles par heure", L"miles per hour"}},
        {L"kn", Cat::Speed, 1852 / 3600.0, 0, L"km/h", {L"kn", L"kt"}, {}, {L"noeud", L"noeuds", L"knot", L"knots"}},   // « 2nd » n'est pas une vitesse
        {L"ft/s", Cat::Speed, 0.3048, 0, L"m/s", {L"ft/s"}, {}, {L"pied par seconde", L"pieds par seconde"}},

        {L"ms", Cat::Time, 1e-3, 0, L"", {L"ms"}, {}, {L"milliseconde", L"millisecondes", L"millisecond", L"milliseconds"}},
        {L"s", Cat::Time, 1, 0, L"", {L"s", L"sec", L"secs"}, {}, {L"seconde", L"secondes", L"second", L"seconds"}},
        {L"min", Cat::Time, 60, 0, L"", {L"min", L"mn", L"mins"}, {}, {L"minute", L"minutes"}},
        {L"h", Cat::Time, 3600, 0, L"", {L"h", L"hr", L"hrs"}, {}, {L"heure", L"heures", L"hour", L"hours"}},
        {L"j", Cat::Time, 86400, 0, L"", {L"j"}, {}, {L"jour", L"jours", L"day", L"days"}},
        {L"sem", Cat::Time, 604800, 0, L"", {L"sem"}, {}, {L"semaine", L"semaines", L"week", L"weeks"}},

        {L"bit", Cat::Data, 0.125, 0, L"o", {L"bit", L"bits"}, {}, {}},
        {L"o", Cat::Data, 1, 0, L"bit", {}, {L"o", L"B"}, {L"octet", L"octets", L"byte", L"bytes"}},
        {L"Ko", Cat::Data, 1e3, 0, L"Kio", {L"Ko", L"ko", L"kB", L"KB"}, {}, {L"kilooctet", L"kilooctets", L"kilobyte", L"kilobytes"}},
        {L"Mo", Cat::Data, 1e6, 0, L"Mio", {L"Mo", L"mo", L"MB"}, {}, {L"megaoctet", L"megaoctets", L"megabyte", L"megabytes"}},
        {L"Go", Cat::Data, 1e9, 0, L"Gio", {L"Go", L"go", L"GB", L"gb"}, {}, {L"gigaoctet", L"gigaoctets", L"gigabyte", L"gigabytes"}},
        {L"To", Cat::Data, 1e12, 0, L"Tio", {L"To", L"to", L"TB", L"tb"}, {}, {L"teraoctet", L"teraoctets", L"terabyte", L"terabytes"}},
        {L"Kio", Cat::Data, 1024.0, 0, L"Ko", {L"Kio", L"kio", L"KiB", L"kib"}, {}, {L"kibioctet", L"kibioctets", L"kibibyte", L"kibibytes"}},
        {L"Mio", Cat::Data, 1048576.0, 0, L"Mo", {L"Mio", L"mio", L"MiB", L"mib"}, {}, {L"mebioctet", L"mebioctets", L"mebibyte", L"mebibytes"}},
        {L"Gio", Cat::Data, 1073741824.0, 0, L"Go", {L"Gio", L"gio", L"GiB", L"gib"}, {}, {L"gibioctet", L"gibioctets", L"gibibyte", L"gibibytes"}},
        {L"Tio", Cat::Data, 1099511627776.0, 0, L"To", {L"Tio", L"tio", L"TiB", L"tib"}, {}, {L"tebioctet", L"tebioctets", L"tebibyte", L"tebibytes"}},

        {L"J", Cat::Energy, 1, 0, L"cal", {L"J"}, {}, {L"joule", L"joules"}},
        {L"kJ", Cat::Energy, 1e3, 0, L"kcal", {L"kJ", L"kj"}, {}, {L"kilojoule", L"kilojoules"}},
        {L"MJ", Cat::Energy, 1e6, 0, L"kWh", {L"MJ"}, {}, {L"megajoule", L"megajoules"}},
        {L"cal", Cat::Energy, 4.184, 0, L"J", {L"cal"}, {}, {L"calorie", L"calories"}},
        {L"kcal", Cat::Energy, 4184, 0, L"kJ", {L"kcal", L"Cal"}, {}, {L"kilocalorie", L"kilocalories"}},
        {L"Wh", Cat::Energy, 3600, 0, L"kJ", {L"Wh"}, {}, {L"wattheure", L"wattheures", L"watt-heure", L"watts-heures"}},
        {L"kWh", Cat::Energy, 3.6e6, 0, L"MJ", {L"kWh", L"kwh"}, {},
         {L"kilowattheure", L"kilowattheures", L"kilowatt-heure", L"kilowatts-heures"}},

        {L"Pa", Cat::Pressure, 1, 0, L"psi", {L"Pa"}, {}, {L"pascal", L"pascals"}},
        {L"hPa", Cat::Pressure, 100, 0, L"mmHg", {L"hPa"}, {}, {L"hectopascal", L"hectopascals"}},
        {L"kPa", Cat::Pressure, 1e3, 0, L"psi", {L"kPa"}, {}, {L"kilopascal", L"kilopascals"}},
        {L"bar", Cat::Pressure, 1e5, 0, L"psi", {L"bar"}, {}, {L"bars"}},
        {L"mbar", Cat::Pressure, 100, 0, L"mmHg", {L"mbar"}, {}, {L"millibar", L"millibars"}},
        {L"atm", Cat::Pressure, 101325, 0, L"bar", {L"atm"}, {}, {L"atmosphere", L"atmospheres"}},
        {L"psi", Cat::Pressure, 6894.757293168, 0, L"bar", {L"psi"}, {}, {}},
        {L"mmHg", Cat::Pressure, 133.322387415, 0, L"hPa", {L"mmHg"}, {},
         {L"millimetre de mercure", L"millimetres de mercure"}},
    };
    return table;
}

// Espaces, y compris insécable (U+00A0), fine insécable (U+202F) et fine (U+2009), entre les milliers.
bool isSpace(wchar_t c) { return c == L' ' || c == L'\t' || c == L' ' || c == L' ' || c == L'\u2009'; }
bool isDigit(wchar_t c) { return c >= L'0' && c <= L'9'; }

// Forme comparée aux noms : minuscules, sans accents, « œ » en « oe », apostrophe droite.
std::wstring foldName(const std::wstring& text) {
    std::wstring out;
    for (wchar_t c : foldForSearch(text)) {
        if (c == L'œ') out += L"oe";
        else if (c == L'’') out += L'\'';
        else out += c;
    }
    return out;
}

const Unit* unitById(const std::wstring& id) {
    for (const Unit& u : units())
        if (u.id == id) return &u;
    return nullptr;
}

// spaced : le texte suivait une espace (les symboles d'une lettre ambigus, comme « K », l'exigent).
const Unit* findUnit(const std::wstring& text, bool spaced) {
    if (text.empty()) return nullptr;
    for (const Unit& u : units()) {
        for (const auto& s : u.symbols)
            if (s == text) return &u;
        if (spaced)
            for (const auto& s : u.spaced)
                if (s == text) return &u;
    }
    const std::wstring f = foldName(text);
    for (const Unit& u : units())
        for (const auto& n : u.names)
            if (n == f) return &u;
    return nullptr;
}

// Durées : la plus grande unité (sauf la sienne et la milliseconde) où le nombre vaut au moins 1 ; sinon l'unité
// juste au-dessus (la plus grande pour la semaine).
const Unit* ladderTarget(const Unit& from, double amount) {
    std::vector<const Unit*> rungs;
    for (const Unit& u : units())
        if (u.cat == Cat::Time && u.id != L"ms" && &u != &from) rungs.push_back(&u);
    std::sort(rungs.begin(), rungs.end(), [](const Unit* a, const Unit* b) { return a->factor > b->factor; });
    const double base = std::abs(amount) * from.factor;
    for (const Unit* u : rungs)
        if (base / u->factor >= 1 - 1e-12) return u;
    const Unit* above = nullptr;
    for (const Unit* u : rungs)
        if (u->factor > from.factor && (!above || u->factor < above->factor)) above = u;
    return above ? above : (rungs.empty() ? nullptr : rungs.front());
}

// Nombre en tête : signe, chiffres (milliers séparés par une espace : « 1 500 »), décimales après « , » ou « . ».
std::optional<double> leadingNumber(const std::wstring& s, std::size_t& pos) {
    while (pos < s.size() && isSpace(s[pos])) ++pos;
    bool negative = false;
    // Moins : trait d'union, signe moins (U+2212) ou tiret demi-cadratin (U+2013) d'un traitement de texte.
    if (pos < s.size() && (s[pos] == L'-' || s[pos] == L'−' || s[pos] == L'\u2013' || s[pos] == L'+')) {
        negative = s[pos] != L'+';
        ++pos;
    }
    std::string digits;
    while (pos < s.size() && isDigit(s[pos])) digits.push_back(char(s[pos++]));
    if (digits.empty()) return std::nullopt;
    if (digits.size() <= 3)   // groupes de trois chiffres après une espace
        while (pos + 3 < s.size() && isSpace(s[pos]) && isDigit(s[pos + 1]) && isDigit(s[pos + 2]) &&
               isDigit(s[pos + 3]) && (pos + 4 >= s.size() || !isDigit(s[pos + 4]))) {
            digits.append({char(s[pos + 1]), char(s[pos + 2]), char(s[pos + 3])});
            pos += 4;
        }
    if (pos + 1 < s.size() && (s[pos] == L',' || s[pos] == L'.') && isDigit(s[pos + 1])) {
        digits.push_back('.');
        ++pos;
        while (pos < s.size() && isDigit(s[pos])) digits.push_back(char(s[pos++]));
    }
    const double v = std::strtod(digits.c_str(), nullptr);
    if (!std::isfinite(v)) return std::nullopt;
    return negative ? -v : v;
}

std::wstring joined(const std::vector<std::wstring>& words, std::size_t from, std::size_t to) {
    std::wstring out;
    for (std::size_t i = from; i < to; ++i) {
        if (i > from) out += L' ';
        out += words[i];
    }
    return out;
}

bool isConnector(const std::wstring& w) {   // casse indifférente : « EN », « In », « TO »
    const std::wstring f = foldName(w);
    return f == L"en" || f == L"in" || f == L"to" || f == L"vers" || w == L"→" || w == L"=";
}

std::optional<UnitConversion> result(double amount, const Unit& from, const Unit& to) {
    const double value = (amount * from.factor + from.offset - to.offset) / to.factor;
    if (!std::isfinite(value)) return std::nullopt;
    return UnitConversion{amount, value, from.id, to.id};
}

} // namespace

std::optional<UnitConversion> convertUnits(std::wstring_view query) {
    std::wstring s;   // flèches isolées par des espaces : « 98,6F→C » devient « 98,6F → C »
    for (std::size_t i = 0; i < query.size(); ++i) {
        if (query[i] == L'→') {
            s += L" → ";
        } else if (query[i] == L'-' && i + 1 < query.size() && query[i + 1] == L'>') {
            s += L" → ";
            ++i;
        } else {
            s += query[i];
        }
    }
    std::size_t pos = 0;
    const auto amount = leadingNumber(s, pos);
    if (!amount) return std::nullopt;
    const bool spaced = pos < s.size() && isSpace(s[pos]);
    std::vector<std::wstring> words;
    for (std::size_t i = pos; i < s.size();) {
        while (i < s.size() && isSpace(s[i])) ++i;
        std::size_t j = i;
        while (j < s.size() && !isSpace(s[j])) ++j;
        if (j > i) words.push_back(s.substr(i, j - i));
        i = j;
    }
    if (words.empty()) return std::nullopt;
    // « <unité> en <unité> » : le premier mot de liaison qui sépare deux unités de même nature.
    for (std::size_t i = 1; i + 1 < words.size(); ++i) {
        if (!isConnector(words[i])) continue;
        const Unit* from = findUnit(joined(words, 0, i), spaced);
        const Unit* to = findUnit(joined(words, i + 1, words.size()), true);
        if (from && to && from->cat == to->cat) return result(*amount, *from, *to);
    }
    const Unit* from = findUnit(joined(words, 0, words.size()), spaced);
    if (!from) return std::nullopt;
    const Unit* to = from->to.empty() ? ladderTarget(*from, *amount) : unitById(from->to);
    if (!to) return std::nullopt;
    return result(*amount, *from, *to);
}

} // namespace md
