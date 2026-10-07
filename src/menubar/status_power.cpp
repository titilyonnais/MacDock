#include "status_power.h"

namespace md {

BatteryInfo batteryFrom(const SYSTEM_POWER_STATUS& s) {
    BatteryInfo b;
    b.present = s.BatteryFlag != 128 && s.BatteryFlag != 255;   // 128 : pas de batterie ; 255 : état inconnu
    b.charging = b.present && (s.BatteryFlag & 8) != 0;
    b.onAC = s.ACLineStatus != 0;
    b.percent = s.BatteryLifePercent <= 100 ? int(s.BatteryLifePercent) : -1;
    return b;
}

BatteryInfo readBattery() {
    SYSTEM_POWER_STATUS s{};
    if (!GetSystemPowerStatus(&s)) return {};
    return batteryFrom(s);
}

} // namespace md
