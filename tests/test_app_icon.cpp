// Icônes des exécutables (plan 49) : tuile arrondie de Tahoe et pictogramme blanc, dessinés par le code, dans un ICO.
#include <windows.h>
#include <objbase.h>

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "minitest.h"
#include "../src/settings/app_icon.h"

namespace {
const std::uint8_t* px(const md::BgraImage& im, int x, int y) { return &im.px[(std::size_t(y) * im.w + x) * 4]; }
std::uint32_t u16(const std::vector<std::uint8_t>& b, std::size_t at) { return b[at] | (b[at + 1] << 8); }
std::uint32_t u32(const std::vector<std::uint8_t>& b, std::size_t at) { return u16(b, at) | (u16(b, at + 2) << 16); }
} // namespace

TEST_CASE(app_icon_tile_and_corners) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (int size : {16, 32, 256}) {
        const md::BgraImage im = md::renderAppIcon(md::AppIconKind::Settings, size);
        REQUIRE(im.w == size && im.h == size && im.px.size() == std::size_t(size) * size * 4);
        CHECK_EQ(int(px(im, 0, 0)[3]), 0);                    // coin : hors du carré arrondi
        CHECK_EQ(int(px(im, size - 1, size - 1)[3]), 0);
        CHECK(px(im, size / 2, size - 1 - size / 10)[3] > 200);   // tuile opaque (près du bas, hors du pictogramme)
    }
    // Réglages : tuile grise (canaux voisins) ; Dock : tuile sombre. Pictogramme blanc au centre de la tuile.
    const md::BgraImage settings = md::renderAppIcon(md::AppIconKind::Settings, 256);
    const std::uint8_t* gray = px(settings, 128, 236);
    CHECK(std::abs(int(gray[0]) - int(gray[2])) < 16 && gray[1] > 60 && gray[1] < 210);
    const md::BgraImage dock = md::renderAppIcon(md::AppIconKind::Dock, 256);
    const std::uint8_t* dark = px(dock, 128, 236);
    CHECK(dark[0] < 70 && dark[1] < 70 && dark[2] < 70 && dark[3] > 200);
    int white = 0;   // des pixels presque blancs : le pictogramme
    for (int y = 64; y < 192; ++y)
        for (int x = 64; x < 192; ++x) white += px(dock, x, y)[0] > 230 && px(dock, x, y)[1] > 230 && px(dock, x, y)[2] > 230;
    CHECK(white > 400);
    CoUninitialize();
}

TEST_CASE(app_icon_ico_container) {
    // En-tête ICO (réservé 0, type 1, nombre d'images), une entrée de 16 octets par taille, données PNG à la suite.
    const std::vector<std::uint8_t> a{1, 2, 3}, b{4, 5, 6, 7};
    const auto ico = md::icoFile({{16, a}, {256, b}});
    REQUIRE(ico.size() == 6 + 2 * 16 + a.size() + b.size());
    CHECK_EQ(u16(ico, 0), 0u);
    CHECK_EQ(u16(ico, 2), 1u);
    CHECK_EQ(u16(ico, 4), 2u);
    CHECK_EQ(int(ico[6]), 16);           // largeur
    CHECK_EQ(int(ico[7]), 16);           // hauteur
    CHECK_EQ(u16(ico, 6 + 6), 32u);      // bits par pixel
    CHECK_EQ(u32(ico, 6 + 8), 3u);       // taille des données
    CHECK_EQ(u32(ico, 6 + 12), 38u);     // décalage : après l'en-tête et les deux entrées
    CHECK_EQ(int(ico[22]), 0);           // 256 : écrit 0
    CHECK_EQ(u32(ico, 22 + 12), 41u);
    CHECK_EQ(int(ico[38]), 1);
    CHECK_EQ(int(ico[41]), 4);
}
