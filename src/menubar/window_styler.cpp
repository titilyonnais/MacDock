#include <dwmapi.h>

#include "../core/log.h"
#include "traffic_window.h"
#include "window_look.h"

namespace md {

namespace {
// Attributs de Windows 11 (SDK 22000+) ; repris ici pour ne pas dépendre de la version des en-têtes.
constexpr DWORD kCorner = 33, kBorderColor = 34, kCaptionColor = 35, kTextColor = 36;
constexpr DWORD kRound = 2, kCornerDefault = 0;
constexpr COLORREF kColorNone = 0xFFFFFFFE, kColorDefault = 0xFFFFFFFF;

bool set(HWND h, DWORD attr, DWORD value) { return SUCCEEDED(DwmSetWindowAttribute(h, attr, &value, sizeof value)); }

BOOL CALLBACK collect(HWND h, LPARAM lp) {
    if (IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}
} // namespace

void WindowStyler::apply(HWND h, const LightsWindowInfo& info, bool dark, UINT dpi) {
    if (!enabled_ || !h) return;
    auto it = touched_.find(Touched{h, dark});
    if (it != touched_.end() && it->dark == dark) return;
    const auto look = macWindowLook(info, dark, dpi);
    if (!look) return;
    bool ok = true;
    if (look->round) ok &= set(h, kCorner, kRound);
    if (look->noBorder) ok &= set(h, kBorderColor, kColorNone);
    if (look->caption) {
        ok &= set(h, kCaptionColor, look->captionColor);
        ok &= set(h, kTextColor, look->textColor);
    }
    if (it != touched_.end()) touched_.erase(it);
    touched_.insert(Touched{h, dark});
    if (!ok) log::info(L"Apparence macOS : refusée en partie par la fenêtre %p (%ls)", h, info.className.c_str());
}

void WindowStyler::applyAll(bool dark) {
    if (!enabled_) return;
    std::vector<HWND> all;
    EnumWindows(collect, reinterpret_cast<LPARAM>(&all));
    for (HWND h : all) apply(h, readInfo(h), dark, effectiveDpi(h));
}

void WindowStyler::restoreAll() {
    for (const Touched& t : touched_) {
        if (!IsWindow(t.h)) continue;
        set(t.h, kCorner, kCornerDefault);
        set(t.h, kBorderColor, kColorDefault);
        set(t.h, kCaptionColor, kColorDefault);
        set(t.h, kTextColor, kColorDefault);
    }
    touched_.clear();
}

void WindowStyler::setEnabled(bool on, bool dark) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) applyAll(dark);
    else restoreAll();
}

} // namespace md
