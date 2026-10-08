#include "sound_play.h"

#include <windows.h>
#include <mmsystem.h>

#include <map>
#include <mutex>
#include <vector>

namespace md {

void playSystemSound(SystemSound s) {
    // Fabriqués une fois et gardés : PlaySound lit le tampon pendant toute la lecture.
    static std::mutex lock;
    static std::map<SystemSound, std::vector<std::uint8_t>> cache;
    const std::vector<std::uint8_t>* wav = nullptr;
    {
        std::lock_guard<std::mutex> guard(lock);
        auto& slot = cache[s];
        if (slot.empty()) slot = wavBytes(synthesizeSound(s, 44100), 44100);
        wav = &slot;
    }
    PlaySoundW(reinterpret_cast<LPCWSTR>(wav->data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

} // namespace md
