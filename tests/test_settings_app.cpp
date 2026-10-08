// App Réglages : modèle (fichiers settings.json et menubar.json), sections, mise en page, rendu des contrôles.
#include <windows.h>

#include <string>

#include "minitest.h"
#include "../src/config/config_store.h"
#include "../src/settings/settings_doc.h"

namespace {
// Dossier temporaire propre à un test (effacé au début).
std::wstring tempDir(const wchar_t* name) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"macdock-settings-" + name;
    CreateDirectoryW(dir.c_str(), nullptr);
    DeleteFileW((dir + L"\\settings.json").c_str());
    DeleteFileW((dir + L"\\menubar.json").c_str());
    return dir;
}

md::json::Value readFile(const std::wstring& path) { return md::loadJsonFile(path).value; }
} // namespace

TEST_CASE(settings_merge_writes_only_changed_keys) {
    // Le fichier garde ses clés inconnues et ce que le Dock y a écrit (épingles) : seule la clé changée est réécrite.
    md::json::Value file = *md::json::parse(
        R"({"tileSize":48,"futureKey":true,"pinned":[{"kind":"app","appId":"x","launch":"C:\\x.exe","name":"X"}]})");
    md::Settings s = md::settingsFromJson(file);
    const md::json::Value before = md::settingsToJson(s);
    s.tileSize = 60;
    const md::json::Value merged = md::mergeChanged(file, before, md::settingsToJson(s));
    CHECK_NEAR(merged.find("tileSize")->asNumber(0), 60.0, 1e-9);
    CHECK(merged.find("futureKey") && merged.find("futureKey")->asBool(false));
    CHECK(md::json::serialize(*merged.find("pinned"), false) == md::json::serialize(*file.find("pinned"), false));
    CHECK(!merged.find("magnification"));   // inchangée : pas ajoutée au fichier
}

TEST_CASE(settings_commit_keeps_pins_written_by_the_dock) {
    const std::wstring dir = tempDir(L"commit");
    // Le Dock a épinglé une app : l'app Réglages, qui change la taille, ne doit pas l'effacer.
    md::saveJsonFileAtomic(dir + L"\\settings.json", *md::json::parse(
        R"({"version":2,"tileSize":48,"pinned":[{"kind":"app","appId":"a","launch":"C:\\a.exe","name":"A"}],"pinnedInitialized":true})"));
    md::SettingsModel after;
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.dock.tileSize = 64; }, &after));
    const md::json::Value f = readFile(dir + L"\\settings.json");
    CHECK_NEAR(f.find("tileSize")->asNumber(0), 64.0, 1e-9);
    REQUIRE(f.find("pinned") && f.find("pinned")->isArray());
    CHECK_EQ(f.find("pinned")->asArray().size(), std::size_t(1));
    CHECK_NEAR(after.dock.tileSize, 64.0, 1e-9);
    // La barre n'a pas changé : son fichier n'est pas créé.
    CHECK(GetFileAttributesW((dir + L"\\menubar.json").c_str()) == INVALID_FILE_ATTRIBUTES);
}

TEST_CASE(settings_commit_creates_a_missing_file) {
    const std::wstring dir = tempDir(L"missing");
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.bar.autohide = true; }));
    const md::json::Value f = readFile(dir + L"\\menubar.json");
    CHECK(f.find("autohide") && f.find("autohide")->asBool(false));
    const md::SettingsModel m = md::loadModel(dir);
    CHECK(m.bar.autohide);
    CHECK(!m.dock.autohide);   // défauts pour le Dock, sans fichier
}

#include "../src/settings/panes.h"

namespace {
const md::RowSpec* rowNamed(const std::vector<md::GroupSpec>& groups, const wchar_t* label) {
    for (const auto& g : groups)
        for (const auto& r : g.rows)
            if (r.label == label) return &r;
    return nullptr;
}
} // namespace

TEST_CASE(settings_panes_list_and_keys) {
    const auto& panes = md::paneList();
    REQUIRE(panes.size() == 11);
    CHECK(panes.front().id == md::PaneId::General);
    CHECK(md::paneFromKey("dock") == md::PaneId::Dock);
    CHECK(md::paneFromKey("menubar") == md::PaneId::MenuBar);
    CHECK(md::paneFromKey("windows") == md::PaneId::Windows);
    CHECK(!md::paneFromKey("nope").has_value());
    CHECK(md::paneInfo(md::PaneId::Dock).ready);
    CHECK(md::paneInfo(md::PaneId::Mods).ready);   // toutes prêtes depuis le plan 42
    int total = 0;   // les groupes de la barre latérale couvrent toutes les sections
    for (int n : md::sidebarGroups()) total += n;
    CHECK_EQ(std::size_t(total), md::paneList().size());
}

TEST_CASE(settings_panes_rows_roundtrip) {
    // Chaque ligne lit ce qu'elle écrit, pour chaque valeur possible (interrupteurs, choix, segments, curseurs).
    md::PaneEnv env;
    env.screens = {L"Écran 1", L"Écran 2"};
    env.screenIds = {L"DISPLAY1", L"DISPLAY2"};
    for (md::PaneId id : {md::PaneId::Dock, md::PaneId::MenuBar, md::PaneId::Windows}) {
        const auto groups = md::paneGroups(id, env);
        CHECK(!groups.empty());
        for (const auto& g : groups)
            for (const auto& r : g.rows) {
                if (r.kind == md::RowKind::Info) continue;
                REQUIRE(r.get && r.set);
                std::vector<double> values;
                if (r.kind == md::RowKind::Switch) values = {1, 0};
                else if (r.kind == md::RowKind::Slider) values = {r.max, r.min, (r.min + r.max) / 2};
                else for (std::size_t i = 0; i < r.choices.size(); ++i) values.push_back(double(i));
                for (double v : values) {
                    md::SettingsModel m;
                    if (r.kind == md::RowKind::Slider) m.dock.tileSize = 16;   // la taille agrandie peut descendre
                    r.set(m, v);
                    CHECK_NEAR(r.get(m), std::round(v / r.step) * r.step, 1e-9);
                }
            }
    }
}

TEST_CASE(settings_panes_dock_specifics) {
    md::PaneEnv env;
    env.screens = {L"Écran 1", L"Écran 2"};
    env.screenIds = {L"DISPLAY1", L"DISPLAY2"};
    const auto dock = md::paneGroups(md::PaneId::Dock, env);
    const md::RowSpec* large = rowNamed(dock, L"Taille agrandie");
    REQUIRE(large && large->enabled);
    md::SettingsModel m;
    m.dock.magnification = false;
    CHECK(!large->enabled(m));   // grisée sans agrandissement, comme sur Mac
    m.dock.magnification = true;
    CHECK(large->enabled(m));
    // Agrandir la taille au-delà de la taille agrandie relève celle-ci (jamais plus petite que les icônes).
    const md::RowSpec* size = rowNamed(dock, L"Taille");
    REQUIRE(size);
    m.dock.largeSize = 80;
    size->set(m, 100);
    CHECK(m.dock.largeSize >= 100);
    // Écran du Dock : « Écran principal » puis les écrans branchés.
    const md::RowSpec* screen = rowNamed(dock, L"Écran du Dock");
    REQUIRE(screen && screen->choices.size() == 3);
    screen->set(m, 2);
    CHECK(m.dock.screen == L"DISPLAY2");
    screen->set(m, 0);
    CHECK(m.dock.screen.empty());
    const md::RowSpec* position = rowNamed(dock, L"Position à l'écran");
    REQUIRE(position && position->kind == md::RowKind::Segmented);
    position->set(m, 0);
    CHECK(m.dock.position == md::DockPosition::Left);
}

#include "../src/ui/ui_layout.h"
#include "../src/ui/ui_theme.h"

TEST_CASE(ui_pane_layout_groups_and_rows) {
    // Deux groupes : sans titre (3 lignes de 36 pt), puis titré (36 et 44 pt) avec une note. Le contenu commence sous
    // la zone de titre ; 18 pt entre groupes ; le titre d'un groupe est posé juste au-dessus de lui.
    std::vector<md::ui::GroupShape> groups(2);
    groups[0].rows = {36, 36, 36};
    groups[1].title = true;
    groups[1].rows = {36, 44};
    groups[1].footer = true;
    const md::ui::PaneLayout l = md::ui::layoutPane(groups);
    REQUIRE(l.groups.size() == 2);
    const float top = md::ui::metrics::contentTop;
    CHECK_NEAR(l.groups[0].top, top, 1e-4);
    CHECK_NEAR(l.groups[0].height, 108.0, 1e-4);
    CHECK_NEAR(l.groups[0].rows[1].top, top + 36, 1e-4);
    const md::ui::GroupBox& g = l.groups[1];
    CHECK(g.titleTop > l.groups[0].top + 108);
    CHECK_NEAR(g.top - (l.groups[0].top + 108), md::ui::metrics::groupGap + md::ui::metrics::groupTitle, 1e-4);
    CHECK_NEAR(g.rows[1].height, 44.0, 1e-4);
    CHECK(g.footerTop >= g.top + g.height);
    CHECK(l.height > g.footerTop);
}

TEST_CASE(ui_slider_value_and_position) {
    // Piste de 100 à 300 pt, valeurs de 16 à 128 au pas de 1 ; bouts et pas respectés.
    CHECK_NEAR(md::ui::sliderValueAt(100, 16, 128, 1, 100, 300), 16.0, 1e-9);
    CHECK_NEAR(md::ui::sliderValueAt(300, 16, 128, 1, 100, 300), 128.0, 1e-9);
    CHECK_NEAR(md::ui::sliderValueAt(50, 16, 128, 1, 100, 300), 16.0, 1e-9);    // avant la piste
    CHECK_NEAR(md::ui::sliderValueAt(200, 16, 128, 1, 100, 300), 72.0, 1e-9);
    CHECK_NEAR(md::ui::sliderKnobX(72, 16, 128, 100, 300), 200.0, 1e-4);
    CHECK_NEAR(md::ui::sliderValueAt(md::ui::sliderKnobX(100, 16, 128, 100, 300), 16, 128, 1, 100, 300), 100.0, 1e-9);
}

TEST_CASE(ui_segments_menu_and_focus) {
    CHECK_EQ(md::ui::segmentAt(105, 100, 400, 3), 0);
    CHECK_EQ(md::ui::segmentAt(250, 100, 400, 3), 1);
    CHECK_EQ(md::ui::segmentAt(399, 100, 400, 3), 2);
    CHECK_EQ(md::ui::segmentAt(401, 100, 400, 3), -1);
    CHECK_EQ(md::ui::menuItemAt(10, 6, 22, 3), 0);    // marge de 6 pt en haut, éléments de 22 pt
    CHECK_EQ(md::ui::menuItemAt(51, 6, 22, 3), 2);
    CHECK_EQ(md::ui::menuItemAt(80, 6, 22, 3), -1);
    // Tab : suivant disponible, en bouclant ; les éléments grisés sont sautés.
    const std::vector<bool> focusable{true, false, true, true};
    CHECK_EQ(md::ui::nextFocus(-1, focusable, false), 0);
    CHECK_EQ(md::ui::nextFocus(0, focusable, false), 2);
    CHECK_EQ(md::ui::nextFocus(3, focusable, false), 0);
    CHECK_EQ(md::ui::nextFocus(0, focusable, true), 3);   // Maj+Tab
    CHECK_EQ(md::ui::nextFocus(0, std::vector<bool>{false, false}, false), -1);
}

TEST_CASE(ui_sidebar_rows_and_palette) {
    // Barre latérale : sections groupées, 28 pt par ligne, un écart entre les groupes.
    const std::vector<float> tops = md::ui::sidebarRowTops({1, 4, 4, 2});
    REQUIRE(tops.size() == 11);
    CHECK_NEAR(tops[1] - tops[0], 28 + md::ui::metrics::sidebarGroupGap, 1e-4);
    CHECK_NEAR(tops[2] - tops[1], 28.0, 1e-4);
    CHECK_EQ(md::ui::sidebarRowAt(tops[3] + 5, tops), 3);
    CHECK_EQ(md::ui::sidebarRowAt(tops[0] - 5, tops), -1);
    const md::ui::Palette light = md::ui::palette(false), dark = md::ui::palette(true);
    CHECK(light.window.r > 0.9f && dark.window.r < 0.2f);   // fond clair / sombre
    CHECK(light.accent.b > 0.9f && light.accent.r < 0.1f);  // bleu système
    CHECK(light.text.a > 0.8f && light.text.r < 0.1f);
}

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include "../src/settings/pane_icons.h"
#include "../src/ui/ui_draw.h"

namespace {
// Bitmap hors écran (1 px = 1 pt), fond de fenêtre ; on dessine, puis on lit les pixels.
struct Canvas {
    Microsoft::WRL::ComPtr<IWICBitmap> bmp;
    Microsoft::WRL::ComPtr<ID2D1RenderTarget> rt;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite;
    UINT w, h;
    std::vector<BYTE> px;
    Canvas(UINT width, UINT height) : w(width), h(height) {
        Microsoft::WRL::ComPtr<IWICImagingFactory> wic;
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
        Microsoft::WRL::ComPtr<ID2D1Factory> d2d;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());
        wic->CreateBitmap(w, h, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
        d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(), &rt);
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite.GetAddressOf()));
    }
    void read() {
        px.resize(std::size_t(w) * h * 4);
        WICRect all{0, 0, int(w), int(h)};
        bmp->CopyPixels(&all, w * 4, UINT(px.size()), px.data());
    }
    const BYTE* at(int x, int y) const { return &px[(std::size_t(y) * w + x) * 4]; }   // B, G, R, A
};
bool bluish(const BYTE* p) { return p[0] > 200 && p[2] < 80; }
bool whiteish(const BYTE* p) { return p[0] > 235 && p[1] > 235 && p[2] > 235; }
bool grayish(const BYTE* p) { return p[0] < 245 && p[0] > 150 && std::abs(int(p[0]) - int(p[2])) < 12; }
} // namespace

TEST_CASE(ui_draw_switch_and_slider) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        const md::ui::Palette pal = md::ui::palette(false);
        Canvas c(240, 80);
        md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"");
        c.rt->BeginDraw();
        c.rt->Clear(D2D1::ColorF(1, 1, 1, 1));
        md::ui::drawSwitch(p, D2D1::RectF(10, 10, 42, 28), 1.0f, false);   // allumé
        md::ui::drawSwitch(p, D2D1::RectF(60, 10, 92, 28), 0.0f, false);   // éteint
        md::ui::drawSlider(p, 10, 210, 60, 0.5f, false);
        REQUIRE(SUCCEEDED(c.rt->EndDraw()));
        c.read();
        CHECK(bluish(c.at(15, 19)));      // piste allumée : accent, à gauche du bouton
        CHECK(whiteish(c.at(33, 19)));    // bouton à droite
        CHECK(grayish(c.at(87, 19)));     // piste éteinte : grise, à droite du bouton
        CHECK(whiteish(c.at(69, 19)));    // bouton à gauche
        CHECK(bluish(c.at(40, 60)));      // curseur : rempli jusqu'au bouton (milieu à 110)
        CHECK(grayish(c.at(190, 60)));    // reste de la piste
        CHECK(whiteish(c.at(110, 60)));   // bouton
    }
    CoUninitialize();
}

TEST_CASE(ui_draw_segments_menu_and_lights) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        const md::ui::Palette pal = md::ui::palette(false);
        Canvas c(320, 200);
        md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"");
        c.rt->BeginDraw();
        c.rt->Clear(D2D1::ColorF(1, 1, 1, 1));
        md::ui::drawSegmented(p, D2D1::RectF(10, 10, 220, 32), {L"Gauche", L"Bas", L"Droite"}, 1);
        md::ui::drawMenu(p, D2D1::RectF(10, 60, 200, 60 + 2 * 6 + 3 * 22), {L"Génie", L"Échelle", L"Windows"}, 0, 1);
        md::ui::drawWindowLights(p, D2D1::Point2F(250, 30), true, false, -1);
        REQUIRE(SUCCEEDED(c.rt->EndDraw()));
        c.read();
        // Segment choisi (milieu) : blanc ; les autres : fond gris du contrôle. Points pris hors du texte.
        CHECK(whiteish(c.at(84, 13)));
        CHECK(grayish(c.at(14, 13)));
        // Menu : l'élément survolé (deuxième) sur l'accent, à gauche de son texte.
        CHECK(bluish(c.at(20, 60 + 6 + 22 + 11)));
        CHECK(!bluish(c.at(20, 60 + 6 + 11)));
        // Pastilles de la fenêtre : rouge au premier centre.
        const BYTE* red = c.at(250, 30);
        CHECK(red[2] > 180 && red[1] < 140);
    }
    CoUninitialize();
}

TEST_CASE(ui_draw_every_pane_tile) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    {
        const md::ui::Palette pal = md::ui::palette(true);
        for (const md::PaneInfo& info : md::paneList()) {
            Canvas c(40, 40);   // tuile de 20 pt à l'échelle 2 : 40 px
            md::ui::Painter p(c.rt.Get(), c.dwrite.Get(), pal, L"");
            c.rt->BeginDraw();
            c.rt->Clear(D2D1::ColorF(0, 0, 0, 0));
            c.rt->SetTransform(D2D1::Matrix3x2F::Scale(2, 2));
            md::drawPaneTile(p, D2D1::RectF(0, 0, 20, 20), info.tile, info.icon);
            REQUIRE(SUCCEEDED(c.rt->EndDraw()));
            c.read();
            int white = 0;
            for (std::size_t i = 0; i < c.px.size(); i += 4) white += c.px[i] > 230 && c.px[i + 1] > 230 && c.px[i + 2] > 230;
            if (white < 20) fprintf(stderr, "tuile %ls : pictogramme vide (%d)\n", info.title.c_str(), white);
            CHECK(white >= 20);                       // pictogramme blanc dessiné
            CHECK(c.at(20, 2)[3] > 200);              // tuile opaque (bord du haut, au milieu)
            CHECK(c.at(0, 0)[3] < 100);               // coin arrondi
        }
    }
    CoUninitialize();
}

TEST_CASE(settings_commit_never_overwrites_an_invalid_file) {
    // Relecture C1 : un settings.json invalide (virgule en trop, écrit à la main) ne doit pas devenir un fichier presque
    // vide, sinon le Dock repart des défauts et réimporte ses épingles : tout serait perdu.
    const std::wstring dir = tempDir(L"invalid");
    const std::string broken = "{\"tileSize\": 64, \"pinned\": [],}";
    HANDLE f = CreateFileW((dir + L"\\settings.json").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD written = 0;
    WriteFile(f, broken.data(), DWORD(broken.size()), &written, nullptr);
    CloseHandle(f);
    md::ModelFiles status;
    md::loadModel(dir, &status);
    CHECK(status.dockInvalid);
    CHECK(!md::commit(dir, [](md::SettingsModel& m) { m.dock.autohide = true; }));
    // Le fichier n'a pas été remplacé (le .bak éventuel du chargement mis à part).
    HANDLE r = CreateFileW((dir + L"\\settings.json").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    char buf[256] = {};
    DWORD read = 0;
    ReadFile(r, buf, sizeof buf - 1, &read, nullptr);
    CloseHandle(r);
    CHECK(std::string(buf, read) == broken);
    // La barre, elle, s'écrit normalement.
    CHECK(md::commit(dir, [](md::SettingsModel& m) { m.bar.autohide = true; }));
}

TEST_CASE(settings_commit_removes_a_key_set_back_to_default) {
    // Relecture I1 : « Écran principal » retire la clé screen ; sans cela le Dock reste sur l'écran 2.
    const std::wstring dir = tempDir(L"erase");
    md::saveJsonFileAtomic(dir + L"\\settings.json", *md::json::parse(R"({"version":2,"screen":"DISPLAY2","futureKey":1})"));
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.dock.screen.clear(); }));
    const md::json::Value f = readFile(dir + L"\\settings.json");
    CHECK(!f.find("screen"));
    CHECK(f.find("futureKey"));   // les clés inconnues restent
    CHECK(md::loadModel(dir).dock.screen.empty());
}

TEST_CASE(settings_commit_migrates_a_v1_file) {
    // Relecture M1 : un fichier v1 (sans version) est migré avant la fusion ; sinon le Dock le migrerait encore à la
    // lecture et ramènerait à 80 une taille agrandie de 128 choisie dans l'app.
    const std::wstring dir = tempDir(L"v1");
    md::saveJsonFileAtomic(dir + L"\\settings.json", *md::json::parse(R"({"largeSize":100,"tileSize":48})"));
    REQUIRE(md::commit(dir, [](md::SettingsModel& m) { m.dock.largeSize = 128; }));
    const md::json::Value f = readFile(dir + L"\\settings.json");
    CHECK(f.find("version") && f.find("version")->asNumber(0) == 2);
    CHECK_NEAR(md::loadModel(dir).dock.largeSize, 128.0, 1e-9);
}
