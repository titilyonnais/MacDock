#include "switcher_logic.h"

#include <algorithm>

#include "../core/strings.h"

namespace md {

namespace {
constexpr std::size_t kMaxRecent = 64;
constexpr double kCellRatio = 1.375, kMaxIcon = 64;
} // namespace

void AppMru::touch(const std::wstring& appId) {
    if (appId.empty()) return;
    recent_.erase(std::remove(recent_.begin(), recent_.end(), appId), recent_.end());
    recent_.insert(recent_.begin(), appId);
    if (recent_.size() > kMaxRecent) recent_.resize(kMaxRecent);
}

std::vector<std::wstring> AppMru::order(const std::vector<std::wstring>& running) const {
    std::vector<std::wstring> out;
    for (const auto& id : recent_)
        if (std::find(running.begin(), running.end(), id) != running.end()) out.push_back(id);
    for (const auto& id : running)
        if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    return out;
}

std::size_t switcherStart(std::size_t count) { return count > 1 ? 1 : 0; }

std::size_t switcherStep(std::size_t selected, std::size_t count, int delta) {
    if (!count) return 0;
    const long long n = static_cast<long long>(count);
    const long long s = static_cast<long long>(std::min(selected, count - 1)) + delta;
    return static_cast<std::size_t>(((s % n) + n) % n);
}

SwitcherGeometry switcherLayout(std::size_t count, double maxWidth) {
    SwitcherGeometry g;
    const double n = double(std::max<std::size_t>(count, 1));
    g.icon = std::max(0.0, std::min(kMaxIcon, (0.9 * maxWidth - 2 * g.pad) / (kCellRatio * n)));
    g.cell = kCellRatio * g.icon;
    g.width = 2 * g.pad + n * g.cell;
    g.height = 2 * g.pad + g.cell + g.labelH;
    return g;
}

int switcherHit(const SwitcherGeometry& g, std::size_t count, double x, double y) {
    if (g.cell <= 0 || y < g.pad || y >= g.pad + g.cell || x < g.pad) return -1;
    const auto i = static_cast<std::size_t>((x - g.pad) / g.cell);
    return i < count ? int(i) : -1;
}

bool SwitchSession::begin(std::size_t count, bool back, double now) {
    active_ = count > 0;
    panel_ = false;
    count_ = count;
    selected_ = back && count ? count - 1 : switcherStart(count);
    start_ = now;
    return active_;
}

void SwitchSession::step(int delta) {
    if (active_) selected_ = switcherStep(selected_, count_, delta);
}

void SwitchSession::select(std::size_t index) {
    if (active_ && index < count_) selected_ = index;
}

bool SwitchSession::removeSelected() {
    if (!active_) return false;
    if (--count_ == 0) {
        active_ = false;
        return false;
    }
    if (selected_ >= count_) selected_ = count_ - 1;
    return true;
}

SwitchSession::Tick SwitchSession::tick(bool altDown, double now) {
    if (!active_) return Tick::Wait;
    if (!altDown) return Tick::Finish;
    if (!panel_ && now - start_ >= kPanelDelay - 1e-9) {
        panel_ = true;
        return Tick::ShowPanel;
    }
    return Tick::Wait;
}

SwitchActivation switcherActivation(bool hidden, const std::vector<bool>& iconic) {
    SwitchActivation a;
    for (std::size_t i = 0; i < iconic.size(); ++i)
        if (hidden || !iconic[i]) a.windows.push_back(i);
    if (a.windows.empty() && !iconic.empty()) {
        a.windows.push_back(0);
        a.restoreFirst = true;
    }
    return a;
}

std::optional<HotkeySpec> parseSwitcherHotkey(const std::wstring& text) {
    std::wstring t;
    for (wchar_t c : toLower(text))
        if (c != L' ') t.push_back(c);
    if (t == L"alt+tab") return HotkeySpec{MOD_ALT, VK_TAB};
    return std::nullopt;
}

} // namespace md
