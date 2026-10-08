// Sons système façon macOS, tous originaux (synthétisés par le code) : durées, volume, fondu, WAV.
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "minitest.h"
#include "../src/sound/sound_synth.h"

namespace {
int peak(const std::vector<std::int16_t>& pcm) {
    int p = 0;
    for (std::int16_t v : pcm) p = std::max(p, std::abs(int(v)));
    return p;
}
} // namespace

TEST_CASE(sound_every_system_sound_is_short_soft_and_clean) {
    using md::SystemSound;
    for (SystemSound s : {SystemSound::Screenshot, SystemSound::EmptyTrash, SystemSound::Volume, SystemSound::Poof}) {
        const auto pcm = md::synthesizeSound(s, 44100);
        CHECK(!pcm.empty());
        CHECK(pcm.size() <= 44100 * 6 / 10);   // jamais plus de 0,6 s
        const int p = peak(pcm);
        CHECK(p > 3000 && p <= 32767 / 2);     // audible, jamais fort (moitié de la pleine échelle au plus)
        CHECK(std::abs(int(pcm.front())) < 400 && std::abs(int(pcm.back())) < 400);   // sans déclic au début ni à la fin
    }
    CHECK(md::synthesizeSound(SystemSound::Volume, 44100).size() < 44100 / 8);   // le « pop » du volume est très bref
}

TEST_CASE(sound_synthesis_is_deterministic) {
    CHECK(md::synthesizeSound(md::SystemSound::EmptyTrash, 44100) == md::synthesizeSound(md::SystemSound::EmptyTrash, 44100));
}

TEST_CASE(sound_wav_header) {
    const std::vector<std::int16_t> pcm{0, 1000, -1000, 0};
    const auto wav = md::wavBytes(pcm, 44100);
    CHECK(wav.size() == 44 + pcm.size() * 2);
    CHECK(std::memcmp(wav.data(), "RIFF", 4) == 0 && std::memcmp(wav.data() + 8, "WAVEfmt ", 8) == 0);
    std::uint32_t rate = 0, dataSize = 0;
    std::uint16_t channels = 0, bits = 0;
    std::memcpy(&channels, wav.data() + 22, 2);
    std::memcpy(&rate, wav.data() + 24, 4);
    std::memcpy(&bits, wav.data() + 34, 2);
    std::memcpy(&dataSize, wav.data() + 40, 4);
    CHECK(channels == 1 && rate == 44100 && bits == 16 && dataSize == pcm.size() * 2);
}
