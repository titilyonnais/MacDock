#include <windows.h>
#include <objbase.h>

#include "minitest.h"
#include "../src/app/dock_controller.h"

namespace {

md::AppIdentity idOf(const wchar_t* exe) {
    md::AppIdentity a;
    a.exePath = exe;
    a.appId = md::makeAppId(L"", exe);
    a.displayName = L"App";
    a.launch = exe;
    return a;
}

// Dock de 3 apps épinglées + séparateur + corbeille, écran 1000 px, échelle 1.
struct Fixture {
    md::AppModel model;
    md::DockController c;
    md::Settings s;
    md::Metrics m;
    Fixture() {
        s.showRecents = false;
        model.setShowRecents(false);
        model.loadPinned({{md::PinKind::App, L"c:\\a.exe", L"C:\\a.exe", L"A"},
                          {md::PinKind::App, L"c:\\b.exe", L"C:\\b.exe", L"B"},
                          {md::PinKind::App, L"c:\\c.exe", L"C:\\c.exe", L"C"}});
        c.init(s, m, &model);
        c.setViewport(1000, UINT(md::DockController::windowHeightPx(s, m, 1)), 1);
    }
    // Point au centre vertical du fond, à x points du centre du Dock.
    POINT at(double xPoints) const {
        auto h = md::DockController::windowHeightPx(s, m, 1);
        double bgBottom = h - m.dockScreenMargin;
        double bgTop = bgBottom - (s.tileSize + 2 * m.dockPadding);
        return POINT{LONG(500 + xPoints), LONG((bgTop + bgBottom) / 2)};
    }
};

} // namespace

TEST_CASE(controller_hit_test_rest_layout) {
    Fixture f;
    // Repos : 3 icônes de 48 + séparateur (15) + corbeille, espacées de 6.
    auto hit = f.c.hitTest(f.at(0));
    REQUIRE(hit.has_value());
    CHECK(f.c.itemAt(*hit)->key == L"app:c:\\c.exe");   // le centre tombe sur la 3e icône (ordre a, b, c, sep, trash)
    CHECK(!f.c.hitTest(POINT{5, 5}).has_value());       // au-dessus du Dock
}

TEST_CASE(controller_zone_follows_magnification) {
    Fixture f;
    auto inside = f.at(0);
    CHECK(f.c.isInsideInteractiveZone(inside));
    POINT above{inside.x, inside.y - LONG(f.s.tileSize)};   // au-dessus du fond au repos
    CHECK(!f.c.isInsideInteractiveZone(above));
    f.c.setCursor(inside);
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 120);
    CHECK(f.c.isInsideInteractiveZone(above));              // les icônes agrandies dépassent du fond
}

TEST_CASE(controller_animates_only_when_needed) {
    Fixture f;
    CHECK(!f.c.tick(1.0 / 60));
    f.c.setCursor(f.at(0));
    CHECK(f.c.tick(1.0 / 60));
    for (int i = 0; i < 240; ++i) f.c.tick(1.0 / 120);
    CHECK(!f.c.tick(1.0 / 60));
}

TEST_CASE(controller_launch_bounce_stops_when_app_runs) {
    Fixture f;
    f.c.startLaunchBounce(L"c:\\a.exe");
    CHECK(f.c.tick(0.1));
    CHECK(f.c.isBouncing(L"c:\\a.exe"));
    f.model.windowOpened(1, idOf(L"C:\\a.exe"));
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 60);   // le rebond en cours se termine
    CHECK(!f.c.isBouncing(L"c:\\a.exe"));
}

TEST_CASE(controller_launch_bounce_times_out) {
    Fixture f;
    f.c.startLaunchBounce(L"c:\\b.exe");
    for (int i = 0; i < int((f.m.launchTimeout + 2) * 60); ++i) f.c.tick(1.0 / 60);
    CHECK(!f.c.isBouncing(L"c:\\b.exe"));
}

TEST_CASE(controller_attention_until_cleared) {
    Fixture f;
    f.model.windowOpened(1, idOf(L"C:\\a.exe"));   // l'attention vient toujours d'une fenêtre ouverte
    f.c.setAttention(L"c:\\a.exe", true);
    for (int i = 0; i < 600; ++i) f.c.tick(1.0 / 60);
    CHECK(f.c.isBouncing(L"c:\\a.exe"));
    f.c.setAttention(L"c:\\a.exe", false);
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 60);
    CHECK(!f.c.isBouncing(L"c:\\a.exe"));
}

TEST_CASE(controller_frame_has_indicator_for_running_app) {
    Fixture f;
    md::IconProvider icons;
    f.model.windowOpened(1, idOf(L"C:\\b.exe"));
    auto frame = f.c.buildFrame(false, icons);
    REQUIRE(frame.icons.size() == 5);
    CHECK(!frame.icons[0].indicator);
    CHECK(frame.icons[1].indicator);
    CHECK(frame.icons[3].separator);
    CHECK(frame.bgRight > frame.bgLeft);
    CHECK_NEAR(frame.bgBottom, md::DockController::windowHeightPx(f.s, f.m, 1) - f.m.dockScreenMargin, 1e-3);
}

TEST_CASE(controller_indicator_is_spaced_from_icon) {
    Fixture f;
    md::IconProvider icons;
    f.model.windowOpened(1, idOf(L"C:\\b.exe"));
    auto frame = f.c.buildFrame(false, icons);
    REQUIRE(frame.icons.size() == 5);
    const auto& ic = frame.icons[1];
    float visibleBottom = ic.cy + ic.size / 2 - ic.size * float(1 - f.m.iconShapeRatio) / 2;
    float dotTop = ic.indicatorY - float(f.m.indicatorDiameter) / 2;
    CHECK(dotTop - visibleBottom >= 5.0f);
    CHECK(ic.indicatorY < frame.bgBottom);
    CHECK(ic.indicatorY > frame.bgBottom - 10);
}

TEST_CASE(controller_tooltip_on_hover) {
    Fixture f;
    md::IconProvider icons;
    f.c.setCursor(f.at(-108));   // 2 icônes à gauche du centre : « A »
    for (int i = 0; i < 60; ++i) f.c.tick(1.0 / 60);
    auto frame = f.c.buildFrame(false, icons);
    CHECK(frame.tooltip.visible);
    CHECK(frame.tooltip.text == L"A");
    CHECK(frame.tooltip.opacity > 0.9f);
}

TEST_CASE(controller_attention_stops_when_app_closes) {
    Fixture f;
    f.model.windowOpened(1, idOf(L"C:\\a.exe"));
    f.c.tick(0.01);
    f.c.setAttention(L"c:\\a.exe", true);
    f.c.tick(0.1);
    f.model.windowClosed(1);   // l'app est fermée sans avoir été activée
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 60);
    CHECK(!f.c.isBouncing(L"c:\\a.exe"));
    CHECK(!f.c.tick(1.0 / 60));
}

TEST_CASE(controller_hit_test_separator) {
    Fixture f;   // a, b, c, séparateur, corbeille
    bool found = false;
    for (int x = -150; x <= 150 && !found; ++x) {
        auto any = f.c.hitTestAny(f.at(x));
        if (any && f.c.itemAt(*any)->kind == md::ItemKind::Separator) {
            found = true;
            CHECK(!f.c.hitTest(f.at(x)).has_value());   // le clic gauche ignore toujours le séparateur
        }
    }
    CHECK(found);
}

TEST_CASE(controller_hidden_dock_slides_out_of_window) {
    Fixture f;
    md::IconProvider icons;
    const float h = float(md::DockController::windowHeightPx(f.s, f.m, 1));
    CHECK(f.c.hitTest(f.at(0)).has_value());
    f.c.setShown(0);
    CHECK(f.c.consumeDirty());
    auto frame = f.c.buildFrame(false, icons);
    CHECK(frame.bgTop >= h);                    // fond entièrement sous la fenêtre
    for (auto& icon : frame.icons) CHECK(icon.cy - icon.size / 2 >= h);
    CHECK(!f.c.hitTest(f.at(0)).has_value());   // un Dock masqué ne reçoit pas de clic
    f.c.setShown(0.5);
    auto half = f.c.buildFrame(false, icons);
    CHECK(half.bgTop < h);
}

namespace {
// Centre (pixels client) de l'élément i au repos.
POINT centerOf(Fixture& f, std::size_t i) {
    md::IconProvider icons;
    auto frame = f.c.buildFrame(false, icons);
    return POINT{LONG(frame.icons[i].cx), LONG(frame.icons[i].cy)};
}
} // namespace

TEST_CASE(controller_drop_exe_between_pinned_opens_gap) {
    Fixture f;
    md::IconProvider icons;
    POINT a = centerOf(f, 0), b = centerOf(f, 1);
    auto rest = f.c.buildFrame(false, icons);
    auto hover = f.c.dropOver(POINT{(a.x + b.x) / 2, a.y}, {L"C:/Tools/nouveau.exe"});
    CHECK(hover.action == md::DropAction::Pin);
    CHECK_EQ(hover.pinIndex, size_t(1));
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 60);
    auto open = f.c.buildFrame(false, icons);
    CHECK((open.bgRight - open.bgLeft) > (rest.bgRight - rest.bgLeft) + f.s.tileSize * 0.8f);   // place ouverte
    f.c.dropLeave();
    for (int i = 0; i < 120; ++i) f.c.tick(1.0 / 60);
    auto closed = f.c.buildFrame(false, icons);
    CHECK(std::abs((closed.bgRight - closed.bgLeft) - (rest.bgRight - rest.bgLeft)) < 1.0f);
}

TEST_CASE(controller_drop_files_on_app_dims_icon) {
    Fixture f;
    md::IconProvider icons;
    auto hover = f.c.dropOver(centerOf(f, 0), {L"C:/doc.txt"});
    CHECK(hover.action == md::DropAction::OpenWith);
    REQUIRE(hover.item.has_value());
    CHECK_EQ(*hover.item, size_t(0));
    auto frame = f.c.buildFrame(false, icons);
    CHECK(frame.icons[0].dim > 0.2f);
    CHECK_EQ(frame.icons[1].dim, 0.0f);
    f.c.dropLeave();
    auto after = f.c.buildFrame(false, icons);
    CHECK_EQ(after.icons[0].dim, 0.0f);
}

TEST_CASE(controller_drop_outside_or_refused_is_none) {
    Fixture f;
    md::IconProvider icons;
    CHECK(f.c.dropOver(POINT{2, 2}, {L"C:/doc.txt"}).action == md::DropAction::None);
    POINT a = centerOf(f, 0), b = centerOf(f, 1);
    // Un document entre deux apps : refusé, sans place ouverte ni icône assombrie.
    CHECK(f.c.dropOver(POINT{(a.x + b.x) / 2, a.y}, {L"C:/doc.txt"}).action == md::DropAction::None);
    auto frame = f.c.buildFrame(false, icons);
    for (auto& icon : frame.icons) CHECK_EQ(icon.dim, 0.0f);
}

TEST_CASE(controller_pressed_icon_is_dimmed) {
    // macOS assombrit l'icône tant que le bouton reste enfoncé.
    Fixture f;
    md::IconProvider icons;
    POINT b = centerOf(f, 1);
    f.c.pointerDown(b);
    auto pressed = f.c.buildFrame(false, icons);
    CHECK(pressed.icons[1].dim > 0.2f);
    f.c.pointerUp(b);
    auto released = f.c.buildFrame(false, icons);
    CHECK_EQ(released.icons[1].dim, 0.0f);
}

namespace {
// Dock vertical : fenêtre de l'épaisseur du Dock, haute de 1000 px.
struct VerticalFixture {
    md::AppModel model;
    md::DockController c;
    md::Settings s;
    md::Metrics m;
    md::IconProvider icons;
    explicit VerticalFixture(md::DockPosition edge) {
        s.showRecents = false;
        s.position = edge;
        model.setShowRecents(false);
        model.loadPinned({{md::PinKind::App, L"c:\\a.exe", L"C:\\a.exe", L"A"},
                          {md::PinKind::App, L"c:\\b.exe", L"C:\\b.exe", L"B"},
                          {md::PinKind::App, L"c:\\c.exe", L"C:\\c.exe", L"C"}});
        c.init(s, m, &model);
        c.setViewport(UINT(md::DockController::windowHeightPx(s, m, 1)), 1000, 1);
    }
    POINT center(std::size_t i) {
        auto f = c.buildFrame(false, icons);
        return POINT{LONG(f.icons[i].cx), LONG(f.icons[i].cy)};
    }
};
} // namespace

TEST_CASE(controller_vertical_hit_test) {
    for (auto edge : {md::DockPosition::Left, md::DockPosition::Right}) {
        VerticalFixture f(edge);
        auto frame = f.c.buildFrame(false, f.icons);
        const float w = float(md::DockController::windowHeightPx(f.s, f.m, 1));
        CHECK((frame.bgBottom - frame.bgTop) > (frame.bgRight - frame.bgLeft));   // Dock debout
        if (edge == md::DockPosition::Left) CHECK(frame.bgLeft < w / 2);           // collé au bord gauche
        else CHECK(frame.bgRight > w / 2);
        CHECK(frame.icons[0].cy < frame.icons[1].cy);                             // de haut en bas
        CHECK(std::abs(frame.icons[0].cx - frame.icons[1].cx) < 0.5f);
        for (std::size_t i = 0; i < 3; ++i) {
            auto hit = f.c.hitTest(f.center(i));
            REQUIRE(hit.has_value());
            CHECK_EQ(*hit, i);
        }
    }
}

TEST_CASE(controller_vertical_drag_remove) {
    // Dock à droite : « Supprimer » en s'éloignant du bord (vers la gauche), pas en glissant le long du Dock.
    VerticalFixture f(md::DockPosition::Right);
    POINT b = f.center(1);
    f.c.pointerDown(b);
    f.c.pointerMove(POINT{b.x - 10, b.y});
    f.c.pointerMove(POINT{b.x - 200, b.y});
    auto away = f.c.pointerUp(POINT{b.x - 200, b.y});
    CHECK(away.kind == md::DragOutcome::Kind::Remove);

    VerticalFixture g(md::DockPosition::Right);
    POINT gb = g.center(1);
    g.c.pointerDown(gb);
    g.c.pointerMove(POINT{gb.x, gb.y - 10});
    g.c.pointerMove(POINT{gb.x, gb.y - 200});
    auto along = g.c.pointerUp(POINT{gb.x, gb.y - 200});
    CHECK(along.kind != md::DragOutcome::Kind::Remove);
}

TEST_CASE(controller_dock_shrinks_to_fit_axis) {
    // Comme macOS : trop d'éléments pour l'écran → tout le Dock rétrécit, rien ne sort de la fenêtre.
    for (auto edge : {md::DockPosition::Left, md::DockPosition::Right, md::DockPosition::Bottom}) {
        md::AppModel model;
        md::DockController c;
        md::Settings s;
        md::Metrics m;
        md::IconProvider icons;
        s.showRecents = false;
        s.position = edge;
        model.setShowRecents(false);
        std::vector<md::PinnedEntry> pins;
        for (int i = 0; i < 25; ++i) {
            std::wstring exe = L"c:\app" + std::to_wstring(i) + L".exe";
            pins.push_back({md::PinKind::App, exe, exe, L"A"});
        }
        model.loadPinned(pins);
        c.init(s, m, &model);
        const UINT thick = UINT(md::DockController::windowHeightPx(s, m, 1));
        const bool vertical = edge != md::DockPosition::Bottom;
        c.setViewport(vertical ? thick : 700, vertical ? 700 : thick, 1);
        auto f = c.buildFrame(false, icons);
        if (vertical) {
            CHECK(f.bgTop >= 0);
            CHECK(f.bgBottom <= 700);
        } else {
            CHECK(f.bgLeft >= 0);
            CHECK(f.bgRight <= 700);
        }
        REQUIRE(!f.icons.empty());
        CHECK(f.icons.front().size < 48);   // cases réduites
        auto hit = c.hitTest(POINT{LONG(f.icons.back().cx), LONG(f.icons.back().cy)});
        CHECK(hit.has_value());             // la dernière icône (Corbeille) reste atteignable
    }
}

TEST_CASE(controller_stack_icon_uses_preview) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);   // icônes Shell réelles
    md::AppModel model;
    md::DockController c;
    md::Settings s;
    md::Metrics m;
    md::IconProvider icons;
    wchar_t win[MAX_PATH];
    GetWindowsDirectoryW(win, MAX_PATH);
    const std::wstring w = win;
    s.showRecents = false;
    model.setShowRecents(false);
    model.loadPinned({{md::PinKind::Stack, L"", w, L"Windows"}});
    c.init(s, m, &model);
    c.setViewport(1000, UINT(md::DockController::windowHeightPx(s, m, 1)), 1);
    std::size_t k = 0;   // la pile suit un séparateur
    while (c.itemAt(k) && c.itemAt(k)->kind != md::ItemKind::Stack) ++k;
    REQUIRE(c.itemAt(k) != nullptr);
    auto folderIcon = c.buildFrame(false, icons).icons[k].image;
    CHECK(folderIcon != nullptr);
    model.setStackPreview(L"stack:" + w, {w + L"\\notepad.exe"});
    auto stackIcon = c.buildFrame(false, icons).icons[k].image;
    CHECK(stackIcon != nullptr);
    CHECK(stackIcon != folderIcon);   // « Pile » : les derniers éléments, pas l'icône du dossier
    model.setStackDisplay(L"stack:" + w, md::StackDisplay::Folder);
    CHECK(c.buildFrame(false, icons).icons[k].image == folderIcon);
    CoUninitialize();
}
