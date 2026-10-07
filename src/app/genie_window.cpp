#include "genie_window.h"

#include <algorithm>

#include "../core/log.h"
#include "thumbnails.h"

namespace md {

namespace {
constexpr wchar_t kGenieClass[] = L"MacDockGenie";
}

GenieWindow::~GenieWindow() {
    finish();
    if (hwnd_) DestroyWindow(hwnd_);
}

bool GenieWindow::ensureWindow(HINSTANCE instance) {
    if (hwnd_) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = kGenieClass;
    RegisterClassExW(&wc);
    // Même style que le Dock : rien n'y est dessiné, seules les miniatures DWM apparaissent ; les clics passent.
    hwnd_ = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED |
                                WS_EX_TRANSPARENT,
                            kGenieClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
    if (!hwnd_) {
        log::warn(L"Génie : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    SetLayeredWindowAttributes(hwnd_, 0, 255, LWA_ALPHA);
    return true;
}

bool GenieWindow::start(HINSTANCE instance, HWND source, const RECT& from, const RECT& toCell, DockPosition edge,
                        MinimizeEffect effect, bool restore, double now, bool slow) {
    finish();
    if (effect == MinimizeEffect::Windows || !IsWindow(source) || !ensureWindow(instance)) return false;
    HTHUMBNAIL first = nullptr;
    if (FAILED(DwmRegisterThumbnail(hwnd_, source, &first))) return false;
    thumbs_.push_back(first);
    if (FAILED(DwmQueryThumbnailSourceSize(first, &src_)) || src_.cx <= 0 || src_.cy <= 0) {
        finish();
        return false;
    }
    source_ = source;
    from_ = from;
    to_ = fitThumbnail(src_.cx, src_.cy, toCell);   // la forme de la miniature dans sa case
    edge_ = edge;
    effect_ = effect;
    restore_ = restore;
    // Assez de bandes pour une courbe lisse (une toutes les 2 px de la fenêtre) : sans marches visibles.
    const LONG extent = edge == DockPosition::Bottom ? from.bottom - from.top : from.right - from.left;
    slices_ = effect == MinimizeEffect::Scale ? 1 : genieSliceCount(extent);
    for (int i = 1; i < slices_; ++i) {
        HTHUMBNAIL t = nullptr;
        if (FAILED(DwmRegisterThumbnail(hwnd_, source, &t))) {
            finish();
            return false;
        }
        thumbs_.push_back(t);
    }
    UnionRect(&box_, &from_, &to_);
    InflateRect(&box_, 2, 2);
    start_ = now;
    duration_ = minimizeDuration(effect, slow);
    running_ = true;
    show(restore ? 1.0 : 0.0);   // première image posée avant d'afficher la fenêtre : pas d'éclair
    SetWindowPos(hwnd_, HWND_TOPMOST, box_.left, box_.top, box_.right - box_.left, box_.bottom - box_.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void GenieWindow::show(double t) {
    const auto slices = minimizeFrame(effect_, src_, from_, to_, edge_, t, slices_);
    for (std::size_t i = 0; i < thumbs_.size(); ++i) {
        DWM_THUMBNAIL_PROPERTIES p{};
        p.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTSOURCE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
        p.opacity = 255;
        p.fSourceClientAreaOnly = FALSE;
        p.fVisible = FALSE;
        if (i < slices.size() && slices[i].dst.right > slices[i].dst.left && slices[i].dst.bottom > slices[i].dst.top) {
            p.fVisible = TRUE;
            p.rcSource = slices[i].src;
            p.rcDestination = slices[i].dst;
            OffsetRect(&p.rcDestination, -box_.left, -box_.top);   // coordonnées de la fenêtre du génie
        }
        DwmUpdateThumbnailProperties(thumbs_[i], &p);
    }
}

bool GenieWindow::step(double now) {
    if (!running_) return false;
    if (!IsWindow(source_)) {   // fenêtre fermée pendant l'animation
        finish();
        return false;
    }
    const double k = duration_ > 0 ? std::clamp((now - start_) / duration_, 0.0, 1.0) : 1.0;
    show(restore_ ? 1 - k : k);
    if (k < 1) return true;
    running_ = false;
    return false;
}

void GenieWindow::finish() {
    running_ = false;
    if (hwnd_ && !thumbs_.empty()) ShowWindow(hwnd_, SW_HIDE);
    for (HTHUMBNAIL t : thumbs_) DwmUnregisterThumbnail(t);
    thumbs_.clear();
}

} // namespace md
