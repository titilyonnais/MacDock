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
