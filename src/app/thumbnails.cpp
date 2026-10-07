#include "thumbnails.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "../core/diag.h"
#include "../core/log.h"

namespace md {

namespace {
// [diag] appels DWM lents (> 3 ms) des miniatures du Dock.
template <class F> HRESULT timedDwm(const wchar_t* what, F&& f) {
    LARGE_INTEGER a, b, q;
    QueryPerformanceCounter(&a);
    const HRESULT hr = f();
    QueryPerformanceCounter(&b);
    QueryPerformanceFrequency(&q);
    const double ms = double(b.QuadPart - a.QuadPart) * 1000.0 / double(q.QuadPart);
    if (diagnosticCapture() && ms > 3) log::info(L"[diag] miniature : %s en %.1f ms", what, ms);
    return hr;
}
} // namespace

RECT fitThumbnail(int srcW, int srcH, const RECT& cell, double ratio) {
    const double cw = double(cell.right - cell.left), ch = double(cell.bottom - cell.top);
    if (srcW <= 0 || srcH <= 0 || cw <= 0 || ch <= 0) return RECT{cell.left, cell.top, cell.left, cell.top};
    const double box = std::min(cw, ch) * ratio;
    const double k = std::min(box / srcW, box / srcH);
    const double w = srcW * k, h = srcH * k;
    const double cx = cell.left + cw / 2, cy = cell.top + ch / 2;
    return RECT{LONG(std::lround(cx - w / 2)), LONG(std::lround(cy - h / 2)), LONG(std::lround(cx - w / 2) + std::lround(w)),
                LONG(std::lround(cy - h / 2) + std::lround(h))};
}

void Thumbnails::clear() {
    for (auto& [w, e] : entries_)
        if (e.id) DwmUnregisterThumbnail(e.id);
    entries_.clear();
    flush();
}

void Thumbnails::flush() {
    for (HTHUMBNAIL id : retired_) DwmUnregisterThumbnail(id);
    retired_.clear();
}

void Thumbnails::sync(HWND dock, RenderFrame& frame, bool visible) {
    std::set<std::uint64_t> seen;
    if (visible) {
        for (auto& icon : frame.icons) {
            if (!icon.window || icon.separator) continue;
            HWND src = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(icon.window));
            if (!IsWindow(src)) continue;
            seen.insert(icon.window);
            if (icon.opacity <= 0.01f) {
                // Case vide le temps d'un génie : la miniature est masquée, pas retirée. Retirer puis réinscrire coûte
                // ~25 ms à chaque fois (mesuré), au départ même de l'animation.
                auto it = entries_.find(icon.window);
                if (it != entries_.end() && it->second.id && it->second.opacity != 0) {
                    DWM_THUMBNAIL_PROPERTIES p{};
                    p.dwFlags = DWM_TNP_VISIBLE;
                    p.fVisible = FALSE;
                    if (SUCCEEDED(timedDwm(L"masquage", [&] { return DwmUpdateThumbnailProperties(it->second.id, &p); }))) {
                        it->second.opacity = 0;
                        it->second.dest = RECT{};
                    }
                }
                continue;
            }
            Entry& e = entries_[icon.window];
            if (!e.id) {
                if (HRESULT hr = timedDwm(L"inscription", [&] { return DwmRegisterThumbnail(dock, src, &e.id); }); FAILED(hr)) {
                    if (trace_) log::info(L"[trace] miniature impossible pour %p (0x%08lx)", static_cast<void*>(src),
                                          static_cast<unsigned long>(hr));
                    e.id = nullptr;
                    continue;
                }
                if (trace_) log::info(L"[trace] miniature enregistrée pour %p", static_cast<void*>(src));
            }
            // Taille lue une fois (une fenêtre réduite ne change pas de taille) : chaque appel DWM peut attendre une
            // composition (5 à 7 ms mesurées à la fin d'une restauration).
            if (e.size.cx <= 0 || e.size.cy <= 0) {
                if (FAILED(timedDwm(L"taille", [&] { return DwmQueryThumbnailSourceSize(e.id, &e.size); }))) e.size = SIZE{};
                if (e.size.cx <= 0 || e.size.cy <= 0) continue;
            }
            const SIZE size = e.size;
            const float h = icon.size / 2;
            RECT cell{LONG(std::lround(icon.cx - h)), LONG(std::lround(icon.cy - h)), LONG(std::lround(icon.cx + h)),
                      LONG(std::lround(icon.cy + h))};
            RECT dest = fitThumbnail(size.cx, size.cy, cell);
            BYTE opacity = BYTE(std::lround(std::clamp(icon.opacity, 0.0f, 1.0f) * 255));
            if (!EqualRect(&dest, &e.dest) || opacity != e.opacity) {
                DWM_THUMBNAIL_PROPERTIES p{};
                p.dwFlags = DWM_TNP_RECTDESTINATION | DWM_TNP_VISIBLE | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
                p.rcDestination = dest;
                p.fVisible = TRUE;
                p.opacity = opacity;
                p.fSourceClientAreaOnly = FALSE;
                if (SUCCEEDED(timedDwm(L"placement", [&] { return DwmUpdateThumbnailProperties(e.id, &p); }))) {
                    e.dest = dest;
                    e.opacity = opacity;
                }
            }
            icon.thumbnail = true;
        }
    }
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        if (it->second.id) {   // masquée tout de suite (sauf si elle l'est déjà), retirée au repos (flush)
            if (it->second.opacity != 0 || !IsRectEmpty(&it->second.dest)) {
                DWM_THUMBNAIL_PROPERTIES p{};
                p.dwFlags = DWM_TNP_VISIBLE;
                p.fVisible = FALSE;
                timedDwm(L"masquage (retrait)", [&] { return DwmUpdateThumbnailProperties(it->second.id, &p); });
            }
            retired_.push_back(it->second.id);
        }
        if (trace_) log::info(L"[trace] miniature retirée");
        it = entries_.erase(it);
    }
}

} // namespace md
