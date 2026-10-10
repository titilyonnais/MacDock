#include <windows.h>
#include <objbase.h>

#include <memory>
#include <vector>

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

TEST_CASE(icons_frame_budget_defers_unready_icons) {
    // Plan 62 : la première image du Dock préparait toutes ses icônes d'un coup (~700 ms, boucle bloquée au démarrage).
    // Budget épuisé : une icône pas encore prête attend l'image suivante (case vide) ; hors budget, elle est préparée.
    md::IconProvider icons;
    icons.beginBudget(0);
    CHECK(icons.get(L"app:essai-budget", L"", 64) == nullptr);
    CHECK(icons.deferred());
    icons.endBudget();
    const auto img = icons.get(L"app:essai-budget", L"", 64);
    CHECK(img != nullptr);
    icons.beginBudget(0);
    CHECK(icons.get(L"app:essai-budget", L"", 64) == img);   // déjà prête : jamais retenue
    CHECK(!icons.deferred());
    icons.endBudget();
}

TEST_CASE(icons_frame_budget_defers_stack_previews) {
    // L'aperçu d'une pile (vignettes de ses derniers fichiers) passe aussi par le budget de l'image du Dock.
    md::IconProvider icons;
    icons.beginBudget(0);
    CHECK(icons.composeStack(L"stack:essai-budget", {md::FileRef(L"C:/nulle-part/essai.txt", 1)}, 64) == nullptr);
    CHECK(icons.deferred());
    icons.endBudget();
}

TEST_CASE(icons_keep_old_image_while_reloading) {
    // Relecture du plan 62 : après un vidage du cache (thème, réglages des icônes), les cases se vidaient le temps que les
    // icônes reviennent une à une. L'ancienne image reste affichée jusqu'à ce que la nouvelle soit prête.
    md::IconProvider icons;
    const auto old = icons.get(L"app:essai-reserve", L"", 64);
    REQUIRE(old != nullptr);
    icons.setDark(true);
    icons.beginBudget(0);
    CHECK(icons.get(L"app:essai-reserve", L"", 64) == old);
    CHECK(icons.deferred());
    icons.endBudget();
    const auto fresh = icons.get(L"app:essai-reserve", L"", 64);
    CHECK(fresh != nullptr);
    CHECK(fresh != old);
}

TEST_CASE(icons_async_requests_instead_of_building) {
    // Plan 67 : pendant le rendu courant du Dock, une icône absente du cache est demandée au fil des icônes (copie des
    // réglages et génération comprises) ; la case attend, puis l'image rangée sert aux images suivantes.
    md::IconProvider icons;
    icons.setDark(true);
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-fil", L"C:/nulle-part/essai.exe", 64) == nullptr);
    CHECK(icons.deferred());
    icons.endAsync();
    REQUIRE(jobs.size() == 1);
    CHECK(jobs[0].cacheKey == L"app:essai-fil|64");
    CHECK(jobs[0].generation == icons.generation());
    CHECK(jobs[0].priority == 0);
    CHECK(jobs[0].spec.kind == md::IconJobKind::App);
    CHECK(jobs[0].spec.key == L"app:essai-fil" && jobs[0].spec.parsingName == L"C:/nulle-part/essai.exe");
    CHECK(jobs[0].spec.px == 64);
    CHECK(jobs[0].spec.style == icons.style());
    CHECK(jobs[0].spec.style.dark);
    // Résultat rangé : en cache, plus de demande.
    auto im = std::make_shared<md::IconProvider::Image>();
    im->size = 64;
    CHECK(icons.adopt(jobs[0].cacheKey, jobs[0].generation, im));
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-fil", L"C:/nulle-part/essai.exe", 64) == im);
    CHECK(!icons.deferred());
    icons.endAsync();
    CHECK(jobs.size() == 1);
}

TEST_CASE(icons_async_outdated_result_rejected) {
    // Réglages changés pendant la préparation : le résultat de l'ancienne génération est jeté, la case redemande.
    md::IconProvider icons;
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-generation", L"", 64) == nullptr);
    icons.endAsync();
    REQUIRE(jobs.size() == 1);
    icons.setDark(true);   // vidage du cache : génération suivante
    CHECK(icons.generation() > jobs[0].generation);
    CHECK(!icons.adopt(jobs[0].cacheKey, jobs[0].generation, std::make_shared<md::IconProvider::Image>()));
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-generation", L"", 64) == nullptr);
    icons.endAsync();
    REQUIRE(jobs.size() == 2);
    CHECK(jobs[1].generation == icons.generation());
    CHECK(jobs[1].spec.style.dark);
}

TEST_CASE(icons_async_keeps_old_image_until_new_arrives) {
    // Pendant un rechargement (thème), l'ancienne image reste dans la case jusqu'à l'arrivée de la nouvelle.
    md::IconProvider icons;
    const auto old = icons.get(L"app:essai-attente", L"", 64);   // préparée ici, hors du mode « fil »
    REQUIRE(old != nullptr);
    icons.setDark(true);
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-attente", L"", 64) == old);
    CHECK(icons.deferred());
    icons.endAsync();
    REQUIRE(jobs.size() == 1);
    const auto fresh = std::make_shared<md::IconProvider::Image>();
    CHECK(icons.adopt(jobs[0].cacheKey, jobs[0].generation, fresh));
    CHECK(icons.get(L"app:essai-attente", L"", 64) == fresh);
}

TEST_CASE(icons_async_stack_previews_after_tiles) {
    // Aperçus de piles et bouton Apps passent aussi par le fil ; les aperçus après les cases.
    md::IconProvider icons;
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.composeStack(L"stack:essai-fil", {md::FileRef(L"C:/nulle-part/essai.txt", 1)}, 64) == nullptr);
    CHECK(icons.appsButton(64) == nullptr);
    icons.endAsync();
    REQUIRE(jobs.size() == 2);
    CHECK(jobs[0].spec.kind == md::IconJobKind::Stack && jobs[0].priority == 1);
    CHECK(jobs[0].spec.key == L"stack:essai-fil" && jobs[0].spec.files.size() == 1);
    CHECK(jobs[1].spec.kind == md::IconJobKind::AppsButton && jobs[1].priority == 0);
}

TEST_CASE(icons_async_adopt_keeps_image_built_meanwhile) {
    // Une image préparée entre-temps sur le fil du Dock (sélecteur ⌥⇥, glisser) n'est pas remplacée par le résultat.
    md::IconProvider icons;
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-double", L"", 64) == nullptr);
    icons.endAsync();
    REQUIRE(jobs.size() == 1);
    const auto built = icons.get(L"app:essai-double", L"", 64);   // synchrone, comme aujourd'hui
    REQUIRE(built != nullptr);
    CHECK(!icons.adopt(jobs[0].cacheKey, jobs[0].generation, std::make_shared<md::IconProvider::Image>()));
    CHECK(icons.get(L"app:essai-double", L"", 64) == built);
}

TEST_CASE(icons_disk_image_shown_while_waiting) {
    // Plan 68 : une image relue sur disque est montrée tout de suite, en attendant la nouvelle, qui la remplace.
    md::IconProvider icons;
    std::vector<md::IconJob> jobs;
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-disque", L"", 64) == nullptr);
    icons.endAsync();
    REQUIRE(jobs.size() == 1);
    const auto fromDisk = std::make_shared<md::IconProvider::Image>();
    CHECK(icons.adoptStale(jobs[0].cacheKey, jobs[0].generation, fromDisk));
    icons.beginAsync([&](md::IconJob j) { jobs.push_back(std::move(j)); });
    CHECK(icons.get(L"app:essai-disque", L"", 64) == fromDisk);   // tout de suite
    CHECK(icons.deferred());                                       // la nouvelle est toujours attendue
    icons.endAsync();
    const auto fresh = std::make_shared<md::IconProvider::Image>();
    CHECK(icons.adopt(jobs[0].cacheKey, jobs[0].generation, fresh));
    CHECK(icons.get(L"app:essai-disque", L"", 64) == fresh);
    CHECK(!icons.adoptStale(jobs[0].cacheKey, jobs[0].generation, fromDisk));   // arrivée après la nouvelle : jetée
    icons.setDark(true);
    CHECK(!icons.adoptStale(jobs[0].cacheKey, jobs[0].generation, fromDisk));   // génération dépassée : jetée
}
