// Sons système façon macOS, tous originaux : synthétisés par le code (aucun son d'Apple), brefs et doux.
#pragma once
#include <cstdint>
#include <vector>

namespace md {

enum class SystemSound {
    Screenshot,   // déclic d'appareil photo (⊞⇧3, ⊞⇧4)
    EmptyTrash,   // froissement de papier (Corbeille vidée)
    Volume,       // « pop » bref (volume réglé au clavier)
    Poof,         // souffle (icône retirée du Dock)
};

// PCM 16 bits mono ; déterministe (même son à chaque fois).
std::vector<std::int16_t> synthesizeSound(SystemSound s, int sampleRate);
// Fichier WAV complet (en-tête RIFF de 44 octets + données), pour PlaySound(SND_MEMORY).
std::vector<std::uint8_t> wavBytes(const std::vector<std::int16_t>& pcm, int sampleRate);

} // namespace md
