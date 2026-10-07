#include "cursor_file.h"

#include <cstring>

namespace md {

namespace {

void put16(std::vector<std::uint8_t>& b, unsigned v) {
    b.push_back(std::uint8_t(v));
    b.push_back(std::uint8_t(v >> 8));
}
void put32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    put16(b, v & 0xFFFF);
    put16(b, v >> 16);
}
void set32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = std::uint8_t(v >> (8 * i));
}
void tag(std::vector<std::uint8_t>& b, const char* id) { b.insert(b.end(), id, id + 4); }

} // namespace

std::vector<std::uint8_t> encodeCur(const std::vector<CursorFrame>& sizes) {
    std::vector<std::uint8_t> out;
    put16(out, 0);
    put16(out, 2);   // curseur
    put16(out, unsigned(sizes.size()));
    std::uint32_t offset = std::uint32_t(6 + 16 * sizes.size());
    std::vector<std::vector<std::uint8_t>> images;
    for (const CursorFrame& f : sizes) {
        const int w = f.image.w, h = f.image.h;
        const std::uint32_t maskRow = std::uint32_t((w + 31) / 32 * 4);
        std::vector<std::uint8_t> img;
        put32(img, 40);
        put32(img, std::uint32_t(w));
        put32(img, std::uint32_t(2 * h));   // image + masque
        put16(img, 1);
        put16(img, 32);
        for (int i = 0; i < 6; ++i) put32(img, 0);
        for (int y = h - 1; y >= 0; --y)   // de bas en haut
            img.insert(img.end(), f.image.px.begin() + std::ptrdiff_t(y) * w * 4, f.image.px.begin() + std::ptrdiff_t(y + 1) * w * 4);
        img.resize(img.size() + std::size_t(maskRow) * h, 0);   // masque ET : l'alpha décide
        out.push_back(std::uint8_t(w >= 256 ? 0 : w));
        out.push_back(std::uint8_t(h >= 256 ? 0 : h));
        out.push_back(0);
        out.push_back(0);
        put16(out, unsigned(f.hotspot.x));
        put16(out, unsigned(f.hotspot.y));
        put32(out, std::uint32_t(img.size()));
        put32(out, offset);
        offset += std::uint32_t(img.size());
        images.push_back(std::move(img));
    }
    for (auto& img : images) out.insert(out.end(), img.begin(), img.end());
    return out;
}

std::vector<std::uint8_t> encodeAni(const std::vector<std::vector<std::uint8_t>>& curFiles, int jiffies) {
    std::vector<std::uint8_t> out;
    tag(out, "RIFF");
    put32(out, 0);   // taille, plus bas
    tag(out, "ACON");
    tag(out, "anih");
    put32(out, 36);
    put32(out, 36);                                    // cbSize
    put32(out, std::uint32_t(curFiles.size()));        // images
    put32(out, std::uint32_t(curFiles.size()));        // pas
    for (int i = 0; i < 4; ++i) put32(out, 0);         // largeur, hauteur, bits, plans : dans les images
    put32(out, std::uint32_t(jiffies));
    put32(out, 1);                                     // AF_ICON : images au format .cur
    tag(out, "LIST");
    const std::size_t listSize = out.size();
    put32(out, 0);
    tag(out, "fram");
    for (const auto& cur : curFiles) {
        tag(out, "icon");
        put32(out, std::uint32_t(cur.size()));
        out.insert(out.end(), cur.begin(), cur.end());
        if (cur.size() % 2) out.push_back(0);          // chunks de longueur paire
    }
    set32(out, listSize, std::uint32_t(out.size() - listSize - 4));
    set32(out, 4, std::uint32_t(out.size() - 8));
    return out;
}

} // namespace md
