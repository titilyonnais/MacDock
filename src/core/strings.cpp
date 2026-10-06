#include "strings.h"

#include <windows.h>

#include <cwctype>

namespace md {

std::string toUtf8(std::wstring_view text) {
    if (text.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(std::string_view text) {
    if (text.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), n);
    return out;
}

std::wstring toLower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) c = wchar_t(std::towlower(c));
    return out;
}

} // namespace md
