// Calcul de Spotlight (logique pure) : « 12*(3+4) » → 84, affiché à la française.
#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace md {

// + - * / × ÷ ^ (droite), parenthèses, moins unaire, % final (pourcentage), virgule ou point décimal.
// nullopt : pas un calcul (texte, nombre seul, expression incomplète, division par zéro, résultat infini).
std::optional<double> evaluateExpression(std::wstring_view text);
// 10 chiffres significatifs, zéros finaux retirés, virgule décimale, espace fine insécable (U+202F) des milliers ;
// notation scientifique au-delà de 1e15 ou en dessous de 1e-9.
std::wstring formatNumber(double v);

} // namespace md
