#include <algorithm>
#include <cmath>

#include "minitest.h"
#include "../src/anim/bounce.h"
#include "../src/anim/spring.h"

TEST_CASE(spring_converges_without_overshoot_explosion) {
    md::Spring s(420, 38);
    s.snap(0);
    s.setTarget(1);
    double maxV = 0;
    for (int i = 0; i < 240; ++i) {
        s.step(1.0 / 120);
        maxV = std::max(maxV, s.value());
    }
    CHECK(s.settled());
    CHECK_NEAR(s.value(), 1, 1e-3);
    CHECK(maxV < 1.15);
}

TEST_CASE(spring_large_dt_is_stable) {
    md::Spring s;
    s.snap(0);
    s.setTarget(1);
    s.step(2.0);
    CHECK(std::isfinite(s.value()));
    CHECK_NEAR(s.value(), 1, 0.05);
}

TEST_CASE(spring_step_returns_false_when_settled) {
    md::Spring s;
    s.snap(3);
    s.setTarget(3);
    CHECK(!s.step(0.016));
}

TEST_CASE(spring_moves_toward_target) {
    md::Spring s;
    s.snap(0);
    s.setTarget(10);
    CHECK(s.step(0.016));
    CHECK(s.value() > 0);
    CHECK(s.value() < 10);
}

TEST_CASE(bounce_launch_shape) {
    CHECK_NEAR(md::launchBounceOffset(0, 0.6, 10), 0, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.3, 0.6, 10), 10, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.6, 0.6, 10), 0, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(0.9, 0.6, 10), 10, 1e-9);
    CHECK_NEAR(md::launchBounceOffset(1.0, 0, 10), 0, 1e-9);
}

TEST_CASE(bounce_attention_pauses) {
    CHECK(md::attentionBounceOffset(0.3, 0.6, 10, 3, 1.2) > 9);
    CHECK_NEAR(md::attentionBounceOffset(2.0, 0.6, 10, 3, 1.2), 0, 1e-9);
    CHECK(md::attentionBounceOffset(3.0 + 0.3, 0.6, 10, 3, 1.2) > 9);
    CHECK_NEAR(md::attentionBounceOffset(1.0, 0.6, 10, 0, 1.2), 0, 1e-9);
}
