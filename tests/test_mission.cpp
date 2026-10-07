// Mission Control : rangement, test de clic, interpolation, raccourci, rendu hors écran.
#include <windows.h>
#include <objbase.h>

#include <vector>

#include "minitest.h"
#include "../src/calib/png_io.h"
#include "../src/mission/mission_view.h"
#include "../src/mission/mission_layout.h"

namespace {
bool inside(const md::MissionRect& r, const md::MissionRect& a) {
    return r.x >= a.x - 1e-6 && r.y >= a.y - 1e-6 && r.x + r.w <= a.x + a.w + 1e-6 && r.y + r.h <= a.y + a.h + 1e-6;
}
bool overlap(const md::MissionRect& a, const md::MissionRect& b) {
    return a.x < b.x + b.w - 1e-6 && b.x < a.x + a.w - 1e-6 && a.y < b.y + b.h - 1e-6 && b.y < a.y + a.h - 1e-6;
}
} // namespace

TEST_CASE(mission_layout_fits_without_overlap) {
    const md::MissionRect area{0, 0, 1920, 1000};
    for (int n : {1, 2, 3, 5, 8, 13, 30}) {
        std::vector<md::MissionRect> wins;
        for (int i = 0; i < n; ++i)
            wins.push_back({double(i * 37 % 900), double(i * 53 % 500), 600.0 + i * 40 % 500, 400.0 + i * 70 % 400});
        wins[0].w = 2560, wins[0].h = 1080;   // 21:9
        if (n > 1) wins[1].w = 500, wins[1].h = 1400;   // très haute
        auto r = md::missionLayout(wins, area, 24);
        REQUIRE(r.size() == wins.size());
        for (int i = 0; i < n; ++i) {
            CHECK(inside(r[i], area));
            CHECK_NEAR(r[i].w / r[i].h, wins[i].w / wins[i].h, 1e-6);   // proportions gardées
            CHECK(r[i].w <= wins[i].w + 1e-6);                           // jamais agrandie
            for (int j = i + 1; j < n; ++j) CHECK(!overlap(r[i], r[j]));
        }
    }
}

TEST_CASE(mission_layout_single_window_real_size_centered) {
    auto r = md::missionLayout({{50, 50, 800, 600}}, {0, 0, 1920, 1000}, 24);
    REQUIRE(r.size() == 1);
    CHECK_NEAR(r[0].w, 800, 1e-6);
    CHECK_NEAR(r[0].x + r[0].w / 2, 960, 1e-6);
    CHECK_NEAR(r[0].y + r[0].h / 2, 500, 1e-6);
}

TEST_CASE(mission_layout_reading_order) {
    // côte à côte : la gauche reste à gauche ; l'une au-dessus de l'autre (trop grandes pour une ligne) : l'ordre vertical reste
    auto r = md::missionLayout({{1000, 0, 800, 600}, {0, 0, 800, 600}}, {0, 0, 1920, 1000}, 24);
    CHECK(r[1].x < r[0].x);
    auto t = md::missionLayout({{0, 600, 1800, 500}, {0, 0, 1800, 500}}, {0, 0, 1920, 1000}, 24);
    CHECK(t[1].y < t[0].y);
}

TEST_CASE(mission_layout_uses_space) {   // 8 grandes fenêtres : elles occupent une bonne part de la zone
    std::vector<md::MissionRect> wins(8, md::MissionRect{0, 0, 1600, 900});
    auto r = md::missionLayout(wins, {0, 0, 1920, 1000}, 24);
    double sum = 0;
    for (auto& x : r) sum += x.w * x.h;
    CHECK(sum > 0.30 * 1920 * 1000);
}

TEST_CASE(mission_layout_empty_and_degenerate) {
    CHECK(md::missionLayout({}, {0, 0, 1920, 1000}, 24).empty());
    auto r = md::missionLayout({{0, 0, 0, 0}, {0, 0, 300, 200}}, {0, 0, 1920, 1000}, 24);
    REQUIRE(r.size() == 2);
    CHECK(r[0].w >= 0 && r[0].h >= 0 && r[0].w == r[0].w);   // ni négatif ni NaN
    auto tiny = md::missionLayout({{0, 0, 300, 200}}, {0, 0, 10, 10}, 24);
    CHECK(tiny[0].w <= 10 && tiny[0].h <= 10);
}

TEST_CASE(mission_area_hit_lerp_ease) {
    const md::MissionRect a = md::missionArea({100, 0, 1000, 800}, 2);
    CHECK_NEAR(a.x, 196, 1e-9);
    CHECK_NEAR(a.y, 128, 1e-9);
    CHECK_NEAR(a.w, 1000 - 192, 1e-9);
    CHECK_NEAR(a.h, 800 - 128 - 96, 1e-9);
    std::vector<md::MissionRect> rects{{0, 0, 10, 10}, {20, 0, 10, 10}};
    CHECK(md::missionHit(rects, 25, 5) == 1);
    CHECK(md::missionHit(rects, 15, 5) == -1);
    const md::MissionRect m = md::lerpRect({0, 0, 10, 10}, {10, 20, 30, 40}, 0.5);
    CHECK_NEAR(m.x, 5, 1e-9);
    CHECK_NEAR(m.h, 25, 1e-9);
    CHECK_NEAR(md::easeOut(0), 0, 1e-9);
    CHECK_NEAR(md::easeOut(1), 1, 1e-9);
    CHECK(md::easeOut(0.5) > 0.5);
    CHECK_NEAR(md::easeOut(2), 1, 1e-9);
}

TEST_CASE(mission_hotkey_parse) {
    auto a = md::parseMissionHotkey(L"ctrl+alt+up");
    REQUIRE(a.has_value());
    CHECK(a->mods == (MOD_CONTROL | MOD_ALT) && a->vk == VK_UP);
    auto b = md::parseMissionHotkey(L"Ctrl+Up");
    REQUIRE(b.has_value());
    CHECK(b->mods == MOD_CONTROL && b->vk == VK_UP);
    auto f = md::parseMissionHotkey(L"f3");
    REQUIRE(f.has_value());
    CHECK(f->mods == 0 && f->vk == VK_F3);
    CHECK(!md::parseMissionHotkey(L"off"));
    CHECK(!md::parseMissionHotkey(L"x"));
}

TEST_CASE(mission_snapshot_draws_windows) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::vector<md::MissionRect> wins{{100, 100, 800, 600}, {900, 200, 700, 500}, {300, 500, 600, 400}};
    auto none = md::missionSnapshot({}, false, 1280, 800, -1);
    auto some = md::missionSnapshot(wins, false, 1280, 800, -1);
    auto hover = md::missionSnapshot(wins, false, 1280, 800, 0);
    REQUIRE(some.w == 1280 && some.h == 800);
    auto rects = md::missionLayout(wins, md::missionArea({0, 0, 1280, 800}, 1), md::kMissionGap,
                                   md::kMissionGap + md::kMissionLabelRoom);
    const md::MissionRect& r = rects[0];
    const std::size_t c = (std::size_t(r.y + r.h / 2) * 1280 + std::size_t(r.x + r.w / 2)) * 4;
    CHECK(some.px[c] != none.px[c]);
    CHECK(hover.px != some.px);
    CoUninitialize();
}

TEST_CASE(mission_layout_row_gap_leaves_room_for_title) {   // la pastille du titre tient entre deux lignes
    std::vector<md::MissionRect> wins(6, md::MissionRect{0, 0, 1200, 800});
    for (int i = 0; i < 6; ++i) wins[i].y = i * 10.0;
    auto r = md::missionLayout(wins, {0, 0, 1920, 1000}, 24, 24 + md::kMissionLabelRoom);
    for (std::size_t i = 0; i < r.size(); ++i)
        for (std::size_t j = 0; j < r.size(); ++j)
            if (r[j].y > r[i].y + r[i].h - 1e-6) CHECK(r[j].y - (r[i].y + r[i].h) >= 24 + md::kMissionLabelRoom - 1e-6);
}

TEST_CASE(mission_wallpaper_kept_at_screen_size) {   // un fond 8K n'est pas gardé en pleine résolution
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    wchar_t dir[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, dir);
    const std::wstring path = std::wstring(dir) + L"macdock-test-wall.png";
    std::vector<std::uint8_t> px(400 * 100 * 4, 255);
    for (int x = 0; x < 400; ++x) px[(50 * 400 + x) * 4] = std::uint8_t(x < 200 ? 0 : 255);   // moitié gauche sans bleu
    REQUIRE(md::writePng(path, px.data(), 400, 100));
    const md::BgraImage im = md::wallpaperCover(path, 50, 50);
    CHECK(im.w == 50 && im.h == 50 && im.px.size() == 50u * 50 * 4);
    CHECK(md::wallpaperCover(L"C:\\nexiste\\pas.png", 50, 50).px.empty());
    DeleteFileW(path.c_str());
    CoUninitialize();
}
