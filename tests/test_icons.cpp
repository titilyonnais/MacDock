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

TEST_CASE(icons_trash_full_differs) {
    // Corbeille vide (SIID_RECYCLER) et pleine (SIID_RECYCLERFULL) : deux icônes système distinctes.
    ComScope com;
    md::IconProvider p;
    auto empty = p.trash(false, 96);
    auto full = p.trash(true, 96);
    REQUIRE(empty != nullptr);
    REQUIRE(full != nullptr);
    CHECK(opaquePixels(*empty) > 96 * 96 / 4);
    CHECK(opaquePixels(*full) > 96 * 96 / 4);
    CHECK(empty->bgra != full->bgra);
}

TEST_CASE(icons_file_image_is_plain_and_sized) {
    // Élément de pile : image Shell (vignette ou icône du type), sans plaque ni forme de Dock.
    ComScope com;
    md::IconProvider p;
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    auto img = p.file(std::wstring(win) + L"\\notepad.exe", 64);
    REQUIRE(img != nullptr);
    CHECK_EQ(img->size, 64);
    CHECK_EQ(img->bgra.size(), std::size_t(64 * 64 * 4));
    CHECK(opaquePixels(*img) > 64 * 64 / 8);
    CHECK(p.file(L"C:\\nexiste\\pas.zzz", 64) != nullptr);   // fichier absent : icône générique du type
}

TEST_CASE(icons_compose_stack_stays_in_cell) {
    // Icône « Pile » : derniers éléments empilés et inclinés, sans déborder de la case.
    ComScope com;
    md::IconProvider p;
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring w = win;
    auto img = p.composeStack(L"stack:test", {w + L"\\notepad.exe", w + L"\\win.ini", w}, 96);
    REQUIRE(img != nullptr);
    CHECK_EQ(img->size, 96);
    CHECK(opaquePixels(*img) > 96 * 96 / 6);
    int edge = 0;
    for (int i = 0; i < 96; ++i)
        for (int k : {0, 95}) {
            edge += img->bgra[(size_t(k) * 96 + i) * 4 + 3] > 8;
            edge += img->bgra[(size_t(i) * 96 + k) * 4 + 3] > 8;
        }
    CHECK_EQ(edge, 0);   // rien sur le bord de la case
    CHECK(p.composeStack(L"stack:test", {w + L"\\notepad.exe", w + L"\\win.ini", w}, 96) == img);   // cache
    CHECK(p.composeStack(L"stack:vide", {}, 96) == nullptr);
}

TEST_CASE(icons_compose_stack_follows_modification) {
    ComScope com;
    md::IconProvider p;
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring np = std::wstring(win) + L"\\notepad.exe";
    auto a = p.composeStack(L"stack:t", {md::FileRef{np, 1}}, 64);
    auto b = p.composeStack(L"stack:t", {md::FileRef{np, 2}}, 64);
    REQUIRE(a != nullptr);
    CHECK(b != a);   // pas de réponse du cache pour un fichier modifié
    CHECK(p.file(np, 64, 1) != p.file(np, 64, 2));
}

TEST_CASE(icons_extension_icon_needs_no_file) {
    ComScope com;
    md::IconProvider p;
    // Nom seul (document récent sur un partage hors ligne) : icône du type, sans accès au disque ni au réseau.
    auto img = p.extensionIcon(L"rapport.docx", 16);
    REQUIRE(img != nullptr);
    CHECK_EQ(img->size, 16);
    CHECK(opaquePixels(*img) > 0);
    CHECK(p.extensionIcon(L"rapport.docx", 16) == img);   // en cache
}
