// Batterie de la barre de menus (GetSystemPowerStatus).
#pragma once
#include <windows.h>

namespace md {

struct BatteryInfo {
    bool present = false;    // l'appareil a une batterie
    bool charging = false;
    bool onAC = true;        // sur secteur
    int percent = -1;        // -1 : inconnu
};

BatteryInfo batteryFrom(const SYSTEM_POWER_STATUS& s);   // pur
BatteryInfo readBattery();

} // namespace md
