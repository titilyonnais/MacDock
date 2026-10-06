#include "crash_policy.h"

namespace md {

bool CrashPolicy::onCrash(double nowSeconds) {
    crashes_.push_back(nowSeconds);
    while (!crashes_.empty() && nowSeconds - crashes_.front() > window_) crashes_.pop_front();
    return int(crashes_.size()) < max_;
}

} // namespace md
