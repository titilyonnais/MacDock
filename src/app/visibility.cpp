#include "visibility.h"

#include <algorithm>

namespace md {

bool Visibility::update(const VisibilityInputs& in, double now) {
    const bool raw = !in.fullscreen && (!in.autohide || in.cursorAtEdge || in.cursorInDock || in.menuOpen || in.dragging);
    if (raw != raw_) {
        raw_ = raw;
        rawSince_ = now;
    }
    immediate_ = in.fullscreen;
    if (target_ != raw_) {
        double delay = immediate_ ? 0 : (raw_ ? t_.showDelay : t_.leaveDelay);
        if (now - rawSince_ >= delay) target_ = raw_;
    }

    const double dt = last_ < 0 ? 0 : std::clamp(now - last_, 0.0, 0.1);
    last_ = now;
    const double before = p_;
    if (target_) p_ = std::min(1.0, p_ + dt / std::max(0.01, t_.showSeconds));
    else p_ = std::max(0.0, p_ - dt / std::max(0.01, t_.hideSeconds));
    return p_ != before || (target_ ? p_ < 1 : p_ > 0);
}

double Visibility::shown() const {
    // Ease-in-out (cubique), comme l'animation du Dock de macOS.
    const double x = p_;
    return x < 0.5 ? 4 * x * x * x : 1 - (-2 * x + 2) * (-2 * x + 2) * (-2 * x + 2) / 2;
}

double Visibility::wakeAt() const {
    if (target_ == raw_) return -1;
    return rawSince_ + (immediate_ ? 0 : (raw_ ? t_.showDelay : t_.leaveDelay));
}

bool coversMonitor(const RECT& w, const RECT& m) {
    return w.left <= m.left + 1 && w.top <= m.top + 1 && w.right >= m.right - 1 && w.bottom >= m.bottom - 1;
}

bool isFullscreenWindow(const RECT& window, const RECT& monitor, bool zoomed, bool hasCaption) {
    return coversMonitor(window, monitor) && !(zoomed && hasCaption);
}

} // namespace md
