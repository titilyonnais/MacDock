#include "spot_calc.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace md {

namespace {

// Analyseur récursif : expr := terme (± terme)* ; terme := puissance (×÷ puissance)* ;
// puissance := unaire (^ puissance)? ; unaire := - puissance | primaire %* ; primaire := nombre | ( expr ).
class Parser {
public:
    explicit Parser(std::wstring_view s) : s_(s) {}

    std::optional<double> run() {
        auto v = expr();
        skip();
        if (!v || pos_ != s_.size() || !ops_ || !std::isfinite(*v)) return std::nullopt;
        return v;
    }

private:
    std::wstring_view s_;
    std::size_t pos_ = 0;
    int ops_ = 0;   // opérateurs binaires et pourcentages : un nombre seul n'est pas un calcul

    void skip() {
        while (pos_ < s_.size() && (s_[pos_] == L' ' || s_[pos_] == L'\t' || s_[pos_] == L' ' || s_[pos_] == L' '))
            ++pos_;
    }
    bool eat(wchar_t c) {
        skip();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::optional<double> expr() {
        auto v = term();
        while (v) {
            if (eat(L'+')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v += *r;
            } else if (eat(L'-') || eat(L'−')) {
                auto r = term();
                if (!r) return std::nullopt;
                *v -= *r;
            } else {
                break;
            }
            ++ops_;
        }
        return v;
    }

    std::optional<double> term() {
        auto v = power();
        while (v) {
            if (eat(L'*') || eat(L'×')) {
                auto r = power();
                if (!r) return std::nullopt;
                *v *= *r;
            } else if (eat(L'/') || eat(L'÷')) {
                auto r = power();
                if (!r || *r == 0) return std::nullopt;
                *v /= *r;
            } else {
                break;
            }
            ++ops_;
        }
        return v;
    }

    std::optional<double> power() {
        auto v = unary();
        if (v && eat(L'^')) {
            auto r = power();   // associatif à droite
            if (!r) return std::nullopt;
            ++ops_;
            return std::pow(*v, *r);
        }
        return v;
    }

    std::optional<double> unary() {
        if (eat(L'-') || eat(L'−')) {   // après la puissance, comme une calculatrice : -2^2 = -4
            auto v = power();
            if (!v) return std::nullopt;
            return -*v;
        }
        if (eat(L'+')) return unary();
        auto v = primary();
        while (v && eat(L'%')) {
            *v /= 100;
            ++ops_;
        }
        return v;
    }

    std::optional<double> primary() {
        if (eat(L'(')) {
            auto v = expr();
            if (!v || !eat(L')')) return std::nullopt;
            return v;
        }
        skip();
        std::string digits;
        bool point = false;
        while (pos_ < s_.size()) {
            const wchar_t c = s_[pos_];
            if (c >= L'0' && c <= L'9') {
                digits.push_back(char(c));
            } else if ((c == L'.' || c == L',') && !point) {
                digits.push_back('.');
                point = true;
            } else {
                break;
            }
            ++pos_;
        }
        if (digits.empty() || digits == ".") return std::nullopt;
        return std::strtod(digits.c_str(), nullptr);
    }
};

} // namespace

std::optional<double> evaluateExpression(std::wstring_view text) { return Parser(text).run(); }

std::wstring formatNumber(double v) {
    if (!std::isfinite(v)) return {};
    if (v == 0) return L"0";
    const double a = std::abs(v);
    wchar_t buf[64];
    if (a >= 1e15 || a < 1e-9) {
        std::swprintf(buf, 64, L"%.10g", v);
        std::wstring s = buf;
        std::replace(s.begin(), s.end(), L'.', L',');
        return s;
    }
    const int decimals = std::clamp(10 - int(std::floor(std::log10(a))) - 1, 0, 15);
    std::swprintf(buf, 64, L"%.*f", decimals, a);
    std::wstring s = buf;
    if (s.find(L'.') != std::wstring::npos) {
        while (s.back() == L'0') s.pop_back();
        if (s.back() == L'.') s.pop_back();
    }
    const std::size_t dot = s.find(L'.');
    std::wstring intPart = s.substr(0, dot), frac = dot == std::wstring::npos ? L"" : s.substr(dot + 1);
    std::wstring grouped;
    for (std::size_t i = 0; i < intPart.size(); ++i) {
        if (i && (intPart.size() - i) % 3 == 0) grouped.push_back(L' ');
        grouped.push_back(intPart[i]);
    }
    std::wstring out = (v < 0 ? L"-" : L"") + grouped;
    if (!frac.empty()) out += L"," + frac;
    return out == L"-0" ? L"0" : out;
}

} // namespace md
