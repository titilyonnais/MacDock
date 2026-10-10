// Texte de l'horloge de la barre, à la française comme macOS (logique pure).
#pragma once
#include <windows.h>

#include <optional>
#include <string>

namespace md {

struct ClockOptions {
    bool weekday = true, date = true, seconds = false, hour24 = true;
};

// « Mer. 7 oct.  14:32 » (deux espaces avant l'heure, comme macOS 26) ; sans jour ni date : « 14:32 » ; 12 h :
// « 2:32 PM ».
std::wstring formatClock(const SYSTEMTIME& t, const ClockOptions& o);
// « 2026-10-09T22:17 » (--snapshot --time) ; jour de la semaine calculé ; nullopt si la date ou l'heure est impossible.
std::optional<SYSTEMTIME> parseClockTime(const std::wstring& text);

} // namespace md
