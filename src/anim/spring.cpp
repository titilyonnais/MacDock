#include "spring.h"

#include <cmath>

namespace md {

namespace {
constexpr double kSubstep = 0.001;
constexpr double kMaxDt = 0.25;
constexpr double kEpsilon = 1e-3;
}

Spring::Spring(double stiffness, double damping) : stiffness_(stiffness), damping_(damping) {}

void Spring::setParams(double stiffness, double damping) {
    stiffness_ = stiffness;
    damping_ = damping;
}

void Spring::snap(double v) {
    value_ = target_ = v;
    velocity_ = 0;
}

bool Spring::settled() const {
    return std::fabs(value_ - target_) < kEpsilon && std::fabs(velocity_) < kEpsilon;
}

bool Spring::step(double dt) {
    if (settled()) {
        value_ = target_;
        velocity_ = 0;
        return false;
    }
    if (!(dt > 0)) return true;
    if (dt > kMaxDt) {   // longue interruption (veille, débogueur) : on termine l'animation
        snap(target_);
        return false;
    }
    for (double t = 0; t < dt; t += kSubstep) {
        double h = (dt - t < kSubstep) ? dt - t : kSubstep;
        double accel = -stiffness_ * (value_ - target_) - damping_ * velocity_;
        velocity_ += accel * h;
        value_ += velocity_ * h;
    }
    if (settled()) {
        value_ = target_;
        velocity_ = 0;
        return false;
    }
    return true;
}

} // namespace md
