#include "minitest.h"
#include "../src/glass/capture_policy.h"

TEST_CASE(capture_region_to_output) {
    md::IRect out = md::toOutputRect({100, 900, 1000, 1080}, {0, 0, 1920, 1080});
    CHECK_EQ(out.left, 100L);
    CHECK_EQ(out.bottom, 1080L);
    md::IRect second = md::toOutputRect({3900, 1400, 4000, 1628}, {3840, 548, 5760, 1628});
    CHECK_EQ(second.left, 60L);
    CHECK_EQ(second.top, 852L);
    md::IRect none = md::toOutputRect({0, 0, 10, 10}, {3840, 548, 5760, 1628});
    CHECK(none.right <= none.left);
}

TEST_CASE(capture_dirty_rect_filter) {
    md::IRect region{0, 900, 1920, 1080};
    md::IRect rects[2] = {{0, 0, 100, 100}, {500, 1000, 600, 1050}};
    CHECK(md::anyIntersects(region, rects, 2));
    CHECK(!md::anyIntersects(region, rects, 1));
    CHECK(!md::intersects({0, 0, 0, 0}, region));
}

TEST_CASE(capture_rotated_output_unsupported) {
    CHECK(md::rotationSupported(0));
    CHECK(md::rotationSupported(1));
    CHECK(!md::rotationSupported(2));
    CHECK(!md::rotationSupported(4));
}

TEST_CASE(capture_sdr_white_scale) {
    CHECK_NEAR(md::sdrWhiteScale(1000), 1.0, 1e-6);
    CHECK_NEAR(md::sdrWhiteScale(3000), 3.0, 1e-6);
    CHECK_NEAR(md::sdrWhiteScale(0), 1.0, 1e-6);
}

TEST_CASE(capture_backoff_grows_and_resets) {
    md::CaptureBackoff b;
    CHECK_EQ(b.nextDelayMs(), 250u);
    CHECK_EQ(b.nextDelayMs(), 500u);
    CHECK_EQ(b.nextDelayMs(), 1000u);
    CHECK_EQ(b.nextDelayMs(), 2000u);
    CHECK_EQ(b.nextDelayMs(), 2000u);
    b.reset();
    CHECK_EQ(b.nextDelayMs(), 250u);
}

TEST_CASE(capture_change_gate_detects_content_changes) {
    // Lignes de 4 octets utiles avec un pas de 8 : le remplissage de fin de ligne est ignoré.
    std::uint8_t a[16] = {1, 2, 3, 4, 9, 9, 9, 9, 5, 6, 7, 8, 9, 9, 9, 9};
    md::ChangeGate gate;
    CHECK(gate.changed(a, 1, 2, 8));    // première image
    CHECK(!gate.changed(a, 1, 2, 8));   // identique
    a[4] = 0;                           // remplissage seulement
    CHECK(!gate.changed(a, 1, 2, 8));
    a[9] = 0;                           // un pixel change
    CHECK(gate.changed(a, 1, 2, 8));
    CHECK(gate.changed(a, 1, 1, 8));    // taille différente
    gate.reset();
    CHECK(gate.changed(a, 1, 1, 8));
}
