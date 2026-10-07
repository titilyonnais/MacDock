// Texte de l'horloge de la barre, à la française comme macOS (logique pure).
#pragma once
#include <windows.h>

#include <string>

namespace md {

struct ClockOptions {
    bool weekday = true, date = true, seconds = false, hour24 = true;
};

// « mer. 7 oct. 14:32 » ; sans jour ni date : « 14:32 » ; 12 h : « 2:32 PM ».
std::wstring formatClock(const SYSTEMTIME& t, const ClockOptions& o);

} // namespace md
