#include "window_fill.h"

#include <cstdlib>

namespace md {

bool fillCandidate(LONG style, LONG exStyle, bool ownProcess) {
    if (ownProcess || (style & WS_CHILD) || (exStyle & WS_EX_TOOLWINDOW)) return false;
    return (style & WS_CAPTION) == WS_CAPTION && (style & WS_THICKFRAME) && (style & WS_MAXIMIZEBOX);
}

MaximizeReply replyToMaximize(bool enabled, bool candidate, bool givenUp, bool wasFilled) {
    if (!enabled || !candidate || givenUp) return MaximizeReply::Ignore;
    return wasFilled ? MaximizeReply::Previous : MaximizeReply::Fill;
}

bool FillGuard::givenUp(HWND h) const {
    const auto it = states_.find(h);
    return it != states_.end() && it->second.quick >= 2;
}

void FillGuard::filled(HWND h, unsigned long long now) {
    State& s = states_[h];
    s.filledAt = now;
    s.filledOnce = true;
}

bool FillGuard::maximized(HWND h, unsigned long long now, unsigned long long lastInput) {
    const auto it = states_.find(h);
    if (it == states_.end() || !it->second.filledOnce) return false;
    State& s = it->second;
    const unsigned long long elapsed = now - s.filledAt;
    const bool quick = elapsed < kRefillMs || (lastInput < s.filledAt && elapsed < kAppRefillMs);
    s.quick = quick ? s.quick + 1 : 0;
    return quick;
}

void FillGuard::ownMaximize(HWND h, unsigned long long now) {
    State& s = states_[h];
    s.own = true;
    s.ownAt = now;
}

bool FillGuard::ours(HWND h, unsigned long long now) {
    const auto it = states_.find(h);
    if (it == states_.end() || !it->second.own) return false;
    it->second.own = false;
    return now - it->second.ownAt < kOwnMs;
}

void FillGuard::forget(HWND h) { states_.erase(h); }

void FillGuard::purge() {
    for (auto it = states_.begin(); it != states_.end();) it = IsWindow(it->first) ? std::next(it) : states_.erase(it);
}

FullscreenStep fullscreenStep(bool saved, const RECT& frame, const RECT& monitor) {
    const bool covers = std::abs(frame.left - monitor.left) <= 1 && std::abs(frame.top - monitor.top) <= 1 &&
                        std::abs(frame.right - monitor.right) <= 1 && std::abs(frame.bottom - monitor.bottom) <= 1;
    return saved && covers ? FullscreenStep::Exit : FullscreenStep::Enter;
}

bool keepSavedFrame(bool saved, const RECT& frame, const RECT& monitor) {
    return saved && std::abs((frame.right - frame.left) - (monitor.right - monitor.left)) <= 1 &&
           std::abs((frame.bottom - frame.top) - (monitor.bottom - monitor.top)) <= 1;
}

bool ZoomTracker::changed(HWND h, bool zoomed, bool iconic) {
    if (iconic) return false;
    const auto it = zoomed_.find(h);
    if (it == zoomed_.end()) {
        zoomed_[h] = zoomed;
        return false;
    }
    const bool became = zoomed && !it->second;
    it->second = zoomed;
    return became;
}

void ZoomTracker::seed(HWND h, bool zoomed) { zoomed_[h] = zoomed; }

void ZoomTracker::forget(HWND h) { zoomed_.erase(h); }

} // namespace md
