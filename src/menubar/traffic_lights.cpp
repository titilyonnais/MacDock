#include "traffic_lights.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace md {

namespace {
constexpr const wchar_t* kShellClasses[] = {L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Progman", L"WorkerW"};
constexpr double kDiameter = 12, kSpacing = 20, kFirst = 20, kTail = 8, kMinTitle = 20, kDefaultTitle = 28;   // points
}

bool wantsLights(const LightsWindowInfo& w, LightsMode mode, UINT dpi) {
    if (mode == LightsMode::Off || w.iconic || w.ownProcess) return false;
    if ((w.style & WS_CAPTION) != WS_CAPTION || !(w.style & WS_SYSMENU) || (w.style & WS_CHILD)) return false;
    if (w.exStyle & WS_EX_TOOLWINDOW) return false;
    for (const wchar_t* c : kShellClasses)
        if (w.className == c) return false;
    if (mode == LightsMode::Standard && w.client.top - w.frame.top < std::lround(kMinTitle * dpi / 96.0)) return false;
    return true;
}

LightsLayout lightsLayout(const RECT& frame, const RECT& client, UINT dpi) {
    const double k = (dpi ? dpi : 96) / 96.0;
    LightsLayout l;
    LONG titleH = client.top - frame.top;
    if (titleH < std::lround(kMinTitle * k)) titleH = std::lround(kDefaultTitle * k);
    const LONG cy = frame.top + titleH / 2;
    const LONG r = std::lround(kDiameter / 2 * k);
    l.radius = double(r);
    for (int i = 0; i < 3; ++i) {
        const LONG cx = frame.left + std::lround((kFirst + kSpacing * i) * k);
        l.circles[i] = RECT{cx - r, cy - r, cx + r, cy + r};
    }
    const LONG lastCx = (l.circles[2].left + l.circles[2].right) / 2;
    l.window = RECT{frame.left + 4, frame.top, lastCx + std::lround((kDiameter / 2 + kTail) * k), frame.top + titleH};
    l.window.right = std::min(l.window.right, frame.right);
    l.window.bottom = std::min(l.window.bottom, frame.bottom);
    l.patch = l.window;
    return l;
}

int hitLight(const LightsLayout& l, POINT p) {
    for (int i = 0; i < 3; ++i) {
        const double cx = (l.circles[i].left + l.circles[i].right) / 2.0, cy = (l.circles[i].top + l.circles[i].bottom) / 2.0;
        const double dx = p.x - cx, dy = p.y - cy;
        if (dx * dx + dy * dy <= (l.radius + 2) * (l.radius + 2)) return i;
    }
    return -1;
}

UINT lightCommand(int light, bool zoomed) {
    switch (light) {
        case 0: return SC_CLOSE;
        case 1: return SC_MINIMIZE;
        default: return zoomed ? SC_RESTORE : SC_MAXIMIZE;
    }
}

std::uint32_t dominantColor(const std::vector<std::uint32_t>& samples) {
    if (samples.empty()) return 0;
    struct Bucket { int n = 0; std::uint64_t r = 0, g = 0, b = 0; std::size_t first = 0; };
    std::map<std::uint32_t, Bucket> buckets;
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const std::uint32_t c = samples[i];
        const std::uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        Bucket& k = buckets[((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)];
        if (k.n == 0) k.first = i;
        ++k.n;
        k.r += r;
        k.g += g;
        k.b += b;
    }
    const Bucket* best = nullptr;
    for (const auto& [key, k] : buckets)
        if (!best || k.n > best->n || (k.n == best->n && k.first < best->first)) best = &k;
    return std::uint32_t(((best->r / best->n) << 16) | ((best->g / best->n) << 8) | (best->b / best->n));
}

} // namespace md
