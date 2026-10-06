// Conversions de chaînes UTF-8 <-> UTF-16.
#pragma once
#include <string>
#include <string_view>

namespace md {

std::string toUtf8(std::wstring_view text);
std::wstring fromUtf8(std::string_view text);   // séquences invalides => U+FFFD
std::wstring toLower(std::wstring_view text);

} // namespace md
