// Politique de relance : abandon au N-ième plantage dans une fenêtre de temps.
#pragma once
#include <deque>

namespace md {

class CrashPolicy {
public:
    explicit CrashPolicy(int maxCrashes = 3, double windowSeconds = 60) : max_(maxCrashes), window_(windowSeconds) {}
    bool onCrash(double nowSeconds);   // true = relancer ; false = abandonner

private:
    int max_;
    double window_;
    std::deque<double> crashes_;
};

} // namespace md
