#include "supervisor.h"

namespace md {

ExitDecision Supervisor::onExit(ChildRole role, unsigned long code, double nowSeconds) {
    if (code == 0) return role == ChildRole::Dock ? ExitDecision::StopAll : ExitDecision::Forget;
    CrashPolicy& policy = role == ChildRole::Dock ? dock_ : menuBar_;
    return policy.onCrash(nowSeconds) ? ExitDecision::Relaunch : ExitDecision::Forget;
}

} // namespace md
