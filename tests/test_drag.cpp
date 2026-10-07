// Glisser-déposer interne : machine à états du contrôleur (index du modèle, retrait, épinglage, annulation).
#include <cmath>

#include "minitest.h"
#include "../src/app/dock_controller.h"

namespace {

using Kind = md::DragOutcome::Kind;

md::AppIdentity idOf(const wchar_t* exe) {
    md::AppIdentity a;
    a.exePath = exe;
    a.appId = md::makeAppId(L"", exe);
    a.displayName = L"App";
    a.launch = exe;
    return a;
}

md::PinnedEntry app(const wchar_t* exe) {
    md::AppIdentity a = idOf(exe);
    return {md::PinKind::App, a.appId, exe, L"App"};
}

struct DragFixture {
    md::AppModel model;
    md::DockController c;
    md::Settings s;
    md::Metrics m;
    explicit DragFixture(std::vector<md::PinnedEntry> pins) {
        s.showRecents = false;
        model.setShowRecents(false);
        model.loadPinned(pins);
        c.init(s, m, &model);
        c.setViewport(1000, UINT(md::DockController::windowHeightPx(s, m, 1)), 1);
    }
    DragFixture() : DragFixture({app(L"C:\\a.exe"), app(L"C:\\b.exe"), app(L"C:\\c.exe")}) {}

    double bgBottom() const { return md::DockController::windowHeightPx(s, m, 1) - m.dockScreenMargin; }
    double bgTop() const { return bgBottom() - (s.tileSize + 2 * m.dockPadding); }
    // Centre au repos de l'élément i (mise en page sans magnification), en pixels de la fenêtre.
    POINT center(std::size_t i) const {
        md::LayoutInput in;
        for (std::size_t k = 0; const md::DockItem* it = c.itemAt(k); ++k)
            in.items.push_back({it->kind == md::ItemKind::Separator});
        in.tileSize = s.tileSize;
        in.gap = m.iconGap;
        in.padding = m.dockPadding;
        in.separatorWidth = m.separatorWidth;
        in.separatorMargin = m.separatorMargin;
        auto r = md::computeLayout(in);
        return POINT{LONG(std::lround(500 + r.items[i].center)), LONG((bgTop() + bgBottom()) / 2)};
    }
    POINT offset(POINT p, LONG dx, LONG dy = 0) const { return POINT{p.x + dx, p.y + dy}; }
    md::DragOutcome drag(std::size_t from, POINT to) {
        c.pointerDown(center(from));
        POINT mid{(center(from).x + to.x) / 2, (center(from).y + to.y) / 2};
        c.pointerMove(offset(center(from), 10));   // franchit le seuil
        c.pointerMove(mid);
        c.pointerMove(to);
        return c.pointerUp(to);
    }
};

} // namespace

TEST_CASE(controller_click_without_drag) {
    DragFixture f;
    f.c.pointerDown(f.center(0));
    f.c.pointerMove(f.offset(f.center(0), 2));
    auto o = f.c.pointerUp(f.offset(f.center(0), 2));
    CHECK(o.kind == Kind::Click);
    CHECK_EQ(o.index, std::size_t(0));
}

TEST_CASE(controller_drag_reorders_pinned) {
    DragFixture f;   // a, b, c, sep:1, corbeille
    auto o = f.drag(0, f.offset(f.center(2), 10));
    CHECK(o.kind == Kind::Move);
    CHECK_EQ(o.fromPinned, std::size_t(0));
    CHECK_EQ(o.toPinned, std::size_t(2));
    auto back = f.drag(2, f.offset(f.center(0), -10));
    CHECK(back.kind == Kind::Move);
    CHECK_EQ(back.fromPinned, std::size_t(2));
    CHECK_EQ(back.toPinned, std::size_t(0));
}

TEST_CASE(controller_drag_maps_to_pinned_index) {
    // pinned_ = a, pile, Apps, b ; affichage : a, Apps, b, x (ouverte), sep:1, pile, corbeille.
    md::PinnedEntry stack{md::PinKind::Stack, L"", L"C:\\Dl", L"Dl"};
    md::PinnedEntry apps{md::PinKind::AppsButton, L"", L"", L"Apps"};
    DragFixture f({app(L"C:\\a.exe"), stack, apps, app(L"C:\\b.exe")});
    f.model.windowOpened(1, idOf(L"C:\\x.exe"));
    REQUIRE(f.c.itemAt(2) != nullptr);
    f.c.tick(0);
    CHECK(f.c.itemAt(2)->key == L"app:c:\\b.exe");
    auto first = f.drag(2, f.offset(f.center(0), -10));   // b avant a
    CHECK(first.kind == Kind::Move);
    CHECK_EQ(first.fromPinned, std::size_t(3));
    CHECK_EQ(first.toPinned, std::size_t(0));
    auto last = f.drag(0, f.offset(f.center(2), 10));     // a après b (dernière épingle avant l'app ouverte)
    CHECK(last.kind == Kind::Move);
    CHECK_EQ(last.fromPinned, std::size_t(0));
    CHECK_EQ(last.toPinned, std::size_t(3));
}

TEST_CASE(controller_drag_above_threshold_removes) {
    DragFixture f;
    LONG up = LONG(f.bgTop() - f.m.dragRemoveDistance - 10);
    auto o = f.drag(0, POINT{f.center(0).x, up});
    CHECK(o.kind == Kind::Remove);
    CHECK(o.key == L"app:c:\\a.exe");
    CHECK(o.poof);
    f.model.windowOpened(7, idOf(L"C:\\b.exe"));
    f.c.tick(0);
    auto running = f.drag(1, POINT{f.center(1).x, up});
    CHECK(running.kind == Kind::Remove);
    CHECK(!running.poof);   // app ouverte : désépinglée, elle reste dans le Dock
}

TEST_CASE(controller_drag_trash_not_draggable) {
    DragFixture f;   // la corbeille est le dernier élément (index 4)
    REQUIRE(f.c.itemAt(4)->kind == md::ItemKind::Trash);
    f.c.pointerDown(f.center(4));
    f.c.pointerMove(f.offset(f.center(4), 0, -60));
    md::IconProvider icons;
    CHECK(!f.c.dragVisual(icons).active);
    auto o = f.c.pointerUp(f.center(4));
    CHECK(o.kind == Kind::Click);
    CHECK_EQ(o.index, std::size_t(4));
}

TEST_CASE(controller_drag_unpinned_running_pins) {
    DragFixture f;
    f.model.windowOpened(3, idOf(L"C:\\x.exe"));   // affichage : a, b, c, x, sep:1, corbeille
    f.c.tick(0);
    REQUIRE(f.c.itemAt(3)->key == L"app:c:\\x.exe");
    auto o = f.drag(3, f.offset(f.center(0), -10));
    CHECK(o.kind == Kind::Pin);
    CHECK(o.appId == L"c:\\x.exe");
    CHECK_EQ(o.toPinned, std::size_t(0));
}

TEST_CASE(controller_drag_cancel_restores) {
    DragFixture f;
    POINT rest = f.center(1);
    f.c.pointerDown(f.center(0));
    f.c.pointerMove(f.offset(f.center(0), 10));
    f.c.pointerMove(f.offset(f.center(2), 10));
    md::IconProvider icons;
    CHECK(f.c.dragVisual(icons).active);
    for (int i = 0; i < 60; ++i) f.c.tick(1.0 / 60);
    auto during = f.c.buildFrame(false, icons);
    CHECK(std::abs(during.icons[1].cx - float(rest.x)) > 10);   // b a glissé pour combler la place de a
    f.c.cancelDrag();
    CHECK(!f.c.dragVisual(icons).active);
    for (int i = 0; i < 240; ++i) f.c.tick(1.0 / 60);
    auto after = f.c.buildFrame(false, icons);
    CHECK(std::abs(after.icons[1].cx - float(rest.x)) < 1);
    CHECK_NEAR(after.icons[0].opacity, 1.0f, 1e-3);
}

TEST_CASE(controller_drag_unpinned_running_back_in_place_is_none) {
    // Un clic qui tremble sur une app ouverte non épinglée ne doit pas l'épingler.
    DragFixture f;
    f.model.windowOpened(3, idOf(L"C:/x.exe"));   // a, b, c, x, sep:1, corbeille
    f.c.tick(0);
    auto o = f.drag(3, f.offset(f.center(3), 12));
    CHECK(o.kind == Kind::None);
    CHECK(f.model.pinnedEntries().size() == 3u);
}

TEST_CASE(controller_drag_unpinned_running_onto_pinned_end_pins) {
    // Relâchée sur la fin de la section épinglée (juste après c), elle s'épingle en dernière position.
    DragFixture f;
    f.model.windowOpened(3, idOf(L"C:/x.exe"));
    f.c.tick(0);
    auto o = f.drag(3, f.offset(f.center(2), 14));
    CHECK(o.kind == Kind::Pin);
    CHECK_EQ(o.toPinned, std::size_t(3));
}
