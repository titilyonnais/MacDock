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

#include "../src/launcher/supervisor.h"

TEST_CASE(supervisor_dock_normal_exit_stops_all) {
    md::Supervisor s;
    // « Quitter MacDock » : la barre de menus s'arrête avec lui.
    CHECK(s.onExit(md::ChildRole::Dock, 0, 0) == md::ExitDecision::StopAll);
}

TEST_CASE(supervisor_menubar_crash_relaunch_then_forget) {
    md::Supervisor s;
    CHECK(s.onExit(md::ChildRole::MenuBar, 0xC0000005, 0) == md::ExitDecision::Relaunch);
    CHECK(s.onExit(md::ChildRole::MenuBar, 0xC0000005, 5) == md::ExitDecision::Relaunch);
    CHECK(s.onExit(md::ChildRole::MenuBar, 0xC0000005, 10) == md::ExitDecision::Forget);
    // Les plantages de la barre ne comptent pas pour le Dock.
    CHECK(s.onExit(md::ChildRole::Dock, 1, 11) == md::ExitDecision::Relaunch);
}

TEST_CASE(supervisor_menubar_normal_exit_forgets) {
    md::Supervisor s;
    // « Quitter la barre des menus » : elle n'est pas relancée, le Dock continue.
    CHECK(s.onExit(md::ChildRole::MenuBar, 0, 0) == md::ExitDecision::Forget);
}

TEST_CASE(supervisor_dock_crash_loop_forgets_dock) {
    md::Supervisor s;
    CHECK(s.onExit(md::ChildRole::Dock, 1, 0) == md::ExitDecision::Relaunch);
    CHECK(s.onExit(md::ChildRole::Dock, 1, 1) == md::ExitDecision::Relaunch);
    // Abandon du Dock seul : la barre de menus reste surveillée.
    CHECK(s.onExit(md::ChildRole::Dock, 1, 2) == md::ExitDecision::Forget);
}
