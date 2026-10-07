#include "thumbnails.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "../core/log.h"

namespace md {

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
}

void Thumbnails::sync(HWND dock, RenderFrame& frame, bool visible) {
    std::set<std::uint64_t> seen;
    if (visible) {
        for (auto& icon : frame.icons) {
            if (!icon.window || icon.separator || icon.opacity <= 0.01f) continue;
            HWND src = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(icon.window));
            if (!IsWindow(src)) continue;
            seen.insert(icon.window);
            Entry& e = entries_[icon.window];
            if (!e.id) {
                if (HRESULT hr = DwmRegisterThumbnail(dock, src, &e.id); FAILED(hr)) {
                    if (trace_) log::info(L"[trace] miniature impossible pour %p (0x%08lx)", static_cast<void*>(src),
                                          static_cast<unsigned long>(hr));
                    e.id = nullptr;
                    continue;
                }
                if (trace_) log::info(L"[trace] miniature enregistrée pour %p", static_cast<void*>(src));
            }
            SIZE size{};
            if (FAILED(DwmQueryThumbnailSourceSize(e.id, &size)) || size.cx <= 0 || size.cy <= 0) continue;
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
                if (SUCCEEDED(DwmUpdateThumbnailProperties(e.id, &p))) {
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
        if (it->second.id) DwmUnregisterThumbnail(it->second.id);
        if (trace_) log::info(L"[trace] miniature retirée");
        it = entries_.erase(it);
    }
}

} // namespace md
