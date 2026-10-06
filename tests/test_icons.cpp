#include <windows.h>
#include <objbase.h>

#include "minitest.h"
#include "../src/icons/icon_provider.h"

namespace {
struct ComScope {
    ComScope() { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); }
    ~ComScope() { CoUninitialize(); }
};

int opaquePixels(const md::IconProvider::Image& img) {
    int n = 0;
    for (size_t i = 3; i < img.bgra.size(); i += 4) n += img.bgra[i] > 0;
    return n;
}
} // namespace

TEST_CASE(icons_extracts_real_exe_icon) {
    ComScope com;
    md::IconProvider p;
    auto img = p.get(L"notepad", L"C:\\Windows\\System32\\notepad.exe", 96);
    REQUIRE(img != nullptr);
    CHECK_EQ(img->size, 96);
    CHECK_EQ(img->bgra.size(), size_t(96 * 96 * 4));
    CHECK(opaquePixels(*img) > 96 * 96 / 4);
    CHECK(p.get(L"notepad", L"C:\\Windows\\System32\\notepad.exe", 96) == img);   // cache
}

TEST_CASE(icons_missing_target_gives_generic_icon) {
    ComScope com;
    md::IconProvider p;
    auto img = p.get(L"absent", L"C:\\nope\\absent.exe", 64);
    REQUIRE(img != nullptr);
    CHECK(opaquePixels(*img) > 64 * 64 / 2);
}

TEST_CASE(icons_strict_mode_keeps_corners_transparent) {
    ComScope com;
    md::IconProvider p;
    p.setStrictTahoe(true);
    auto img = p.get(L"notepad", L"C:\\Windows\\System32\\notepad.exe", 128);
    REQUIRE(img != nullptr);
    CHECK_EQ(int(img->bgra[3]), 0);                       // coin haut-gauche hors du squircle
    CHECK_EQ(int(img->bgra[(1 * 128 + 64) * 4 + 3]), 0);  // marge de la grille Apple au-dessus de la forme
    CHECK(img->bgra[(64 * 128 + 64) * 4 + 3] > 0);        // centre opaque
}

TEST_CASE(icons_apps_button_is_drawn) {
    md::IconProvider p;
    auto img = p.appsButton(64);
    REQUIRE(img != nullptr);
    CHECK(opaquePixels(*img) > 64 * 64 / 2);
}
