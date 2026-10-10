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
    // Entre la date et l'heure, l'écart de la barre de macOS 26 (~10 pt d'encre en SF Pro 13 pt) : deux espaces et une
    // espace ultra-fine.
    if (!out.empty()) out += L"  \u200A";
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

} // namespace md
