#include <dwmapi.h>

#include <vector>

#include "../core/log.h"
#include "traffic_window.h"
#include "window_look.h"

namespace md {

namespace {
// Attributs de Windows 11 (SDK 22000+) ; repris ici pour ne pas dépendre de la version des en-têtes.
constexpr DWORD kDarkMode = 20, kCorner = 33, kBorderColor = 34, kCaptionColor = 35, kTextColor = 36, kBackdrop = 38;
constexpr DWORD kRound = 2;
constexpr COLORREF kColorNone = 0xFFFFFFFE, kColorDefault = 0xFFFFFFFF;

bool set(HWND h, DWORD attr, DWORD value) { return SUCCEEDED(DwmSetWindowAttribute(h, attr, &value, sizeof value)); }

DWORD processOf(HWND h) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    return pid;
}

BOOL CALLBACK collect(HWND h, LPARAM lp) {
    if (IsWindowVisible(h) && !GetWindow(h, GW_OWNER)) reinterpret_cast<std::vector<HWND>*>(lp)->push_back(h);
    return TRUE;
}
} // namespace

void WindowStyler::restore(const Touched& t) {
    if (!IsWindow(t.h) || processOf(t.h) != t.pid) return;   // fenêtre disparue, ou HWND repris par une autre
    if (t.rounded || t.squared) set(t.h, kCorner, t.corner);
    set(t.h, kBorderColor, kColorDefault);
    if (t.captioned) {
        set(t.h, kCaptionColor, kColorDefault);
        set(t.h, kTextColor, kColorDefault);
    }
}

void WindowStyler::apply(HWND h, const LightsWindowInfo& info, bool dark, UINT dpi) {
    if (!enabled_ || !h) return;
    const DWORD pid = processOf(h);
    Touched key;
    key.h = h;
    auto it = touched_.find(key);
    if (it != touched_.end() && it->pid != pid) {   // HWND réutilisé : une autre fenêtre
        touched_.erase(it);
        it = touched_.end();
    }
    // Mode clair ou sombre de la fenêtre elle-même (une app peut être sombre dans un Windows clair), et son fond.
    DWORD own = 0, backdrop = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(h, kDarkMode, &own, sizeof own))) dark = own != 0;
    if (FAILED(DwmGetWindowAttribute(h, kBackdrop, &backdrop, sizeof backdrop))) backdrop = 0;
    const auto look = macWindowLook(info, dark, dpi, int(backdrop));
    if (!look) {   // plus éligible (passée en plein écran sans bordure…) : rendue à Windows
        const bool square = squareInFullscreen(info);
        if (it != touched_.end() && it->squared && square) return;
        DWORD corner = 0;   // préférence de l'app, rendue au retour
        if (it != touched_.end()) {
            corner = it->corner;
            restore(*it);
            touched_.erase(it);
        } else if (FAILED(DwmGetWindowAttribute(h, kCorner, &corner, sizeof corner))) {
            corner = 0;
        }
        if (square) {   // plein écran de macOS : coins carrés (plan 72)
            Touched s = key;
            s.pid = pid;
            s.corner = corner;
            s.squared = set(h, kCorner, 1 /* DWMWCP_DONOTROUND */);
            if (s.squared) touched_.insert(s);
        }
        return;
    }
    if (it != touched_.end() && it->dark == dark && it->backdrop == backdrop && !it->squared) return;
    Touched t = it != touched_.end() ? *it : key;
    t.dark = dark;
    t.pid = pid;
    t.backdrop = backdrop;
    if (it == touched_.end() || it->squared) {   // première fois : on lit le choix de l'app avant de toucher aux coins
        DWORD corner = it != touched_.end() ? it->corner : 0;   // sortie du plein écran : celui d'avant
        if (it == touched_.end() && FAILED(DwmGetWindowAttribute(h, kCorner, &corner, sizeof corner))) corner = 0;
        t.corner = corner;
        t.rounded = look->round && shouldRoundCorners(corner);
        if (it != touched_.end() && !t.rounded) set(h, kCorner, corner);   // carrée par nous : rendue à son choix
        t.squared = false;
    }
    bool ok = true;
    if (t.rounded) ok &= set(h, kCorner, kRound);
    if (look->noBorder) ok &= set(h, kBorderColor, kColorNone);
    if (look->caption) {
        ok &= set(h, kCaptionColor, look->captionColor);
        ok &= set(h, kTextColor, look->textColor);
        t.captioned = true;
    } else if (t.captioned) {   // barre laissée à l'app (Mica arrivé depuis) : notre couleur seulement est retirée
        set(h, kCaptionColor, kColorDefault);
        set(h, kTextColor, kColorDefault);
        t.captioned = false;
    }
    if (it != touched_.end()) touched_.erase(it);
    touched_.insert(t);
    if (!ok) log::info(L"Apparence macOS : refusée en partie par la fenêtre %p (%ls)", h, info.className.c_str());
}

void WindowStyler::applyAll(bool dark) {
    if (!enabled_) return;
    for (auto it = touched_.begin(); it != touched_.end();)   // fenêtres fermées depuis
        it = IsWindow(it->h) ? std::next(it) : touched_.erase(it);
    std::vector<HWND> all;
    EnumWindows(collect, reinterpret_cast<LPARAM>(&all));
    for (HWND h : all) {
        Touched key;
        key.h = h;
        const auto it = touched_.find(key);
        const bool caption = (GetWindowLongPtrW(h, GWL_STYLE) & WS_CAPTION) == WS_CAPTION;
        if (it == touched_.end() && !caption) continue;   // sans barre de titre : rien à lire (OpenProcess évité)
        if (it != touched_.end() && caption && it->dark == dark && it->pid == processOf(h)) continue;
        apply(h, readInfo(h), dark, effectiveDpi(h));
    }
}

void WindowStyler::restoreAll() {
    for (const Touched& t : touched_) restore(t);
    touched_.clear();
}

void WindowStyler::setEnabled(bool on, bool dark) {
    if (on == enabled_) return;
    enabled_ = on;
    if (on) applyAll(dark);
    else restoreAll();
}

} // namespace md
