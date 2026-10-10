#include "clock_format.h"

#include <cwchar>

namespace md {

std::wstring formatClock(const SYSTEMTIME& t, const ClockOptions& o) {
    static const wchar_t* kDays[] = {L"Dim.", L"Lun.", L"Mar.", L"Mer.", L"Jeu.", L"Ven.", L"Sam."};
    static const wchar_t* kMonths[] = {L"janv.", L"févr.", L"mars", L"avr.", L"mai", L"juin",
                                       L"juil.", L"août", L"sept.", L"oct.", L"nov.", L"déc."};
    std::wstring out;
    if (o.weekday && t.wDayOfWeek < 7) out += kDays[t.wDayOfWeek];
    if (o.date && t.wMonth >= 1 && t.wMonth <= 12) {
        if (!out.empty()) out += L' ';
        out += std::to_wstring(t.wDay) + L' ' + kMonths[t.wMonth - 1];
    }
    // Entre la date et l'heure, deux espaces : avec les chiffres à chasse fixe de la barre, l'écart d'encre de macOS 26
    // (10,35 pt en SF Pro 13 pt).
    if (!out.empty()) out += L"  ";
    int hour = t.wHour;
    const wchar_t* suffix = L"";
    if (!o.hour24) {
        suffix = hour >= 12 ? L" PM" : L" AM";
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    wchar_t buf[32];
    if (o.seconds) swprintf_s(buf, L"%d:%02d:%02d", hour, int(t.wMinute), int(t.wSecond));
    else swprintf_s(buf, L"%d:%02d", hour, int(t.wMinute));
    out += buf;
    out += suffix;
    return out;
}

std::optional<SYSTEMTIME> parseClockTime(const std::wstring& text) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0;
    if (swscanf_s(text.c_str(), L"%d-%d-%dT%d:%d", &y, &mo, &d, &h, &mi) != 5) return std::nullopt;
    if (y < 1601 || y > 30827 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59)
        return std::nullopt;
    SYSTEMTIME t{};
    t.wYear = WORD(y);
    t.wMonth = WORD(mo);
    t.wDay = WORD(d);
    t.wHour = WORD(h);
    t.wMinute = WORD(mi);
    FILETIME ft{};   // l'aller-retour refuse un 31 avril et calcule le jour de la semaine
    if (!SystemTimeToFileTime(&t, &ft) || !FileTimeToSystemTime(&ft, &t)) return std::nullopt;
    return t;
}

} // namespace md
