#include "minitest.h"
#include "../src/launcher/crash_policy.h"

TEST_CASE(crash_policy_gives_up_on_third_crash_within_window) {
    md::CrashPolicy p;
    CHECK(p.onCrash(0));
    CHECK(p.onCrash(10));
    CHECK(!p.onCrash(20));
}

TEST_CASE(crash_policy_forgets_old_crashes) {
    md::CrashPolicy p;
    CHECK(p.onCrash(0));
    CHECK(p.onCrash(10));
    CHECK(p.onCrash(75));
    CHECK(p.onCrash(80));
}

TEST_CASE(crash_policy_custom_limits) {
    md::CrashPolicy p(1, 5);
    CHECK(!p.onCrash(0));
}
