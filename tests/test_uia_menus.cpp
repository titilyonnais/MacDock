// Barre de menus : menus lus par UI Automation (choix des fenêtres interrogées, fil de travail, lecture des titres).
#include <windows.h>

#include <atomic>
#include <memory>

#include "minitest.h"
#include "menu_app.h"
#include "../src/menubar/app_menus.h"
#include "../src/menubar/uia_menu.h"

TEST_CASE(uia_probe_skips_chromium_firefox) {
    CHECK(md::shouldProbeUia(L"Notepad"));
    CHECK(md::shouldProbeUia(L"Qt5152QWindowIcon"));
    CHECK(!md::shouldProbeUia(L"Chrome_WidgetWin_1"));
    CHECK(!md::shouldProbeUia(L"Chrome_WidgetWin_0"));
    CHECK(!md::shouldProbeUia(L"MozillaWindowClass"));
    CHECK(!md::shouldProbeUia(L"MozillaDialogClass"));
    CHECK(!md::shouldProbeUia(L""));
    CHECK(!md::shouldProbeUia(L"Progman"));
    CHECK(!md::shouldProbeUia(L"CabinetWClass"));
}

TEST_CASE(menus_real_uia_actions_carry_path) {
    md::BarContext c;
    c.appName = L"Bloc-notes";
    c.source = md::MenuSource::Uia;
    c.menuOwner = 7;
    md::RawMenuItem zoomIn{L"Zoom avant", L"Ctrl+Plus"};
    zoomIn.position = 0;
    md::RawMenuItem zoom{L"Zoom"};
    zoom.popup = true;
    zoom.position = 2;
    zoom.children = {zoomIn};
    md::RawMenuItem view{L"Affichage"};
    view.popup = true;
    view.position = 1;
    view.children = {zoom};
    md::RawMenuItem file{L"Fichier"};   // titre pas encore lu : une entrée grisée
    file.popup = true;
    file.position = 0;
    c.real = {file, view};
    auto m = md::buildBarMenus(c);
    REQUIRE(m.menus.size() >= 4);
    REQUIRE(m.menus[2].model.items.size() == 1);
    CHECK(!m.menus[2].model.items[0].enabled);
    const auto& sub = m.menus[3].model.items.at(0).submenu;
    REQUIRE(sub.size() == 1);
    const auto& a = m.actions.at(sub[0].id);
    CHECK(a.kind == md::ActionKind::UiaInvoke);
    CHECK(a.arg == L"Zoom avant");
    CHECK(a.path == (std::vector<int>{1, 2, 0}));
    CHECK_EQ(a.window, std::uint64_t(7));
}

TEST_CASE(uia_worker_call_is_bounded) {
    md::UiaWorker worker;
    REQUIRE(worker.start());
    auto done = std::make_shared<std::atomic<int>>(0);
    const ULONGLONG start = GetTickCount64();
    CHECK(!worker.call([done](md::UiaMenus&) { Sleep(400); ++*done; }, 50));   // trop long : abandonné
    CHECK(GetTickCount64() - start < 300);
    CHECK(worker.call([done](md::UiaMenus&) { ++*done; }, 2000));   // passe après le précédent
    CHECK_EQ(done->load(), 2);
    worker.stop();
}

TEST_CASE(uia_menus_titles_from_win32_window) {
    test::MenuApp app;
    REQUIRE(app.hwnd != nullptr);
    md::UiaWorker worker;
    REQUIRE(worker.start());
    std::vector<md::RawMenuItem> titles;
    CHECK(worker.call([&](md::UiaMenus& uia) { titles = uia.titles(app.hwnd); }, 5000));
    worker.stop();
    REQUIRE(titles.size() == 2);
    CHECK(titles[0].text == L"Fichier");
    CHECK(titles[1].text == L"Édition");
    CHECK_EQ(titles[1].position, 1);
    CHECK(titles[1].popup);
}
