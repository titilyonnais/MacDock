// Sprites du glisser-déposer (icône tirée, étiquette « Supprimer ») et nuage « poof ».
#include <cstdint>

#include "minitest.h"
#include "render_fixtures.h"
#include "../src/render/sprite_renderer.h"

namespace {
md::IconProvider::Image square(int px) {
    md::IconProvider::Image img;
    img.size = px;
    img.bgra.assign(size_t(px) * px * 4, 255);
    return img;
}
std::uint64_t alphaSum(const std::vector<std::uint8_t>& bgra) {
    std::uint64_t s = 0;
    for (size_t i = 3; i < bgra.size(); i += 4) s += bgra[i];
    return s;
}
} // namespace

TEST_CASE(sprite_label_adds_height) {
    fixtures::ComScope com;
    md::SpriteRenderer r;
    UINT w1 = 0, h1 = 0, w2 = 0, h2 = 0;
    auto plain = r.dragSprite(square(96), 96, L"", 2, false, L"Segoe UI", w1, h1);
    auto labeled = r.dragSprite(square(96), 96, L"Supprimer", 2, false, L"Segoe UI", w2, h2);
    REQUIRE(!plain.empty());
    REQUIRE(!labeled.empty());
    CHECK_EQ(plain.size(), size_t(w1) * h1 * 4);
    CHECK(h2 > h1 + 20);
    CHECK(w2 >= w1);
    CHECK(alphaSum(labeled) > alphaSum(plain));   // la capsule de l'étiquette est dessinée
}

TEST_CASE(poof_fades_out) {
    fixtures::ComScope com;
    md::SpriteRenderer r;
    auto early = r.poofFrame(0.2, 128);
    auto late = r.poofFrame(0.9, 128);
    REQUIRE(early.size() == size_t(128 * 128 * 4));
    REQUIRE(late.size() == early.size());
    CHECK(alphaSum(early) > 0);
    CHECK(alphaSum(late) * 3 < alphaSum(early));
}
