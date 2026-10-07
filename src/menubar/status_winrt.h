// Radios (Wi-Fi, Bluetooth) et lecture en cours, par les API WinRT de Windows. Bloquant : à appeler sur un fil MTA
// (StatusHub), jamais sur le fil STA de la barre.
#pragma once
#include <string>

namespace md {

struct RadioInfo {
    bool wifiPresent = false, wifiOn = false;
    bool btPresent = false, btOn = false;
};

struct MediaInfo {
    bool present = false;   // une app expose une lecture (navigateur, Spotify…)
    bool playing = false;
    std::wstring title, artist;
};

RadioInfo readRadios();
bool setRadio(bool bluetooth, bool on);   // false : radio absente ou refus du système
MediaInfo readMedia();
bool mediaCommand(int button);            // 0 précédent, 1 lecture/pause, 2 suivant

} // namespace md
