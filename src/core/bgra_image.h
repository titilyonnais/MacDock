// Image BGRA en mémoire (aperçus, curseurs, fonds d'écran dessinés par le code).
#pragma once
#include <cstdint>
#include <vector>

namespace md {

struct BgraImage {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;   // BGRA, alpha non prémultiplié
};

} // namespace md
