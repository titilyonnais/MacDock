#include "genie_window.h"

#include <algorithm>

#include "../core/log.h"
#include "window_capture.h"
#include "thumbnails.h"

namespace md {

namespace {
constexpr wchar_t kGenieClass[] = L"MacDockGenie";
}

GenieWindow::~GenieWindow() {
    finish();
    if (hwnd_) DestroyWindow(hwnd_);
}

void GenieWindow::prepare(HINSTANCE instance) {
    if (ensureWindow(instance)) gpu_.prepare(instance);
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
    UnionRect(&box_, &from_, &to_);
    InflateRect(&box_, 2, 2);
    // Rendu GPU lancé d'abord (capture sur un fil) ; les bandes couvrent l'attente : peu s'il doit arriver vite,
    // toutes (une toutes les 4 px) s'il est absent, ou s'il tarde (growStrips).
    gpuStarted_ = gpu_.begin(instance, source, box_);
    const LONG extent = edge == DockPosition::Bottom ? from.bottom - from.top : from.right - from.left;
    fullSlices_ = effect == MinimizeEffect::Scale ? 1 : genieSliceCount(extent);
    slices_ = genieStripTarget(fullSlices_, gpuStarted_, 0);
    rows_ = effect == MinimizeEffect::Scale ? 1 : std::clamp(int(extent / 3), 48, 256);   // maillage GPU : une rangée / 3 px
    for (int i = 1; i < slices_; ++i) {
        HTHUMBNAIL t = nullptr;
        if (FAILED(DwmRegisterThumbnail(hwnd_, source, &t))) {
            finish();
            return false;
        }
        thumbs_.push_back(t);
    }
    start_ = now;
    elapsed_ = 0;
    duration_ = minimizeDuration(effect, slow);
    running_ = true;
    stripsHidden_ = false;
    show(restore ? 1.0 : 0.0);   // première image posée avant d'afficher la fenêtre : pas d'éclair
    SetWindowPos(hwnd_, HWND_TOPMOST, box_.left, box_.top, box_.right - box_.left, box_.bottom - box_.top,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    return true;
}

void GenieWindow::show(double t) {
    if (gpu_.frame(genieMesh(effect_, src_, from_, to_, edge_, t, rows_)) && gpu_.framesShown() >= 2) {
        if (!stripsHidden_) ShowWindow(hwnd_, SW_HIDE);   // une image plus tard : le GPU est sûrement à l'écran
        stripsHidden_ = true;
        return;
    }
    if (stripsHidden_) {   // GPU perdu en route : les bandes reprennent
        ShowWindow(hwnd_, SW_SHOWNA);
        stripsHidden_ = false;
        gpuStarted_ = false;
    }
    growStrips();
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

void GenieWindow::growStrips() {
    const int want = genieStripTarget(fullSlices_, gpuStarted_, elapsed_);
    while (int(thumbs_.size()) < want) {
        HTHUMBNAIL t = nullptr;
        if (FAILED(DwmRegisterThumbnail(hwnd_, source_, &t))) break;
        thumbs_.push_back(t);
    }
    slices_ = int(thumbs_.size());
}

bool GenieWindow::step(double now) {
    if (!running_) return false;
    elapsed_ = now - start_;
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
    gpu_.end();
    stripsHidden_ = false;
    if (hwnd_ && !thumbs_.empty()) ShowWindow(hwnd_, SW_HIDE);
    for (HTHUMBNAIL t : thumbs_) DwmUnregisterThumbnail(t);
    thumbs_.clear();
}

namespace {
double probeClock() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return double(c.QuadPart) / double(f.QuadPart);
}
} // namespace

bool genieLiveProbe(HINSTANCE instance) {
    HWND probe = createMinimizedProbe(instance);
    if (!probe) return false;
    RECT pr{};
    GetWindowRect(probe, &pr);
    WINDOWPLACEMENT wp{sizeof wp};
    GetWindowPlacement(probe, &wp);
    const POINT at{GetSystemMetrics(SM_XVIRTUALSCREEN) - 4000, GetSystemMetrics(SM_YVIRTUALSCREEN)};
    const RECT from{at.x, at.y, at.x + 640, at.y + 400}, cell{at.x + 296, at.y + 700, at.x + 344, at.y + 748};
    GenieWindow g;
    bool ok = true;
    for (int run = 0; run < 2 && ok; ++run) {
    const double t0 = probeClock();
    g.prepare(instance);
    const double prepared = probeClock();
    ok = g.start(instance, probe, from, cell, DockPosition::Bottom, MinimizeEffect::Genie, false, probeClock(), false);
    const double started = probeClock();
    int frames = 0;
    double gpuAt = -1, last = started, worst = 0;
    while (ok && g.step(probeClock())) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        DCompositionWaitForCompositorClock(0, nullptr, 50);
        const double now = probeClock();
        worst = std::max(worst, now - last);
        last = now;
        ++frames;
        if (gpuAt < 0 && g.onGpu()) gpuAt = now - started;
    }
    const double total = probeClock() - started;
    g.finish();
    log::info(L"Sonde du génie : préparation %.1f ms, départ %.1f ms, GPU après %.1f ms, %d images en %.0f ms (pire écart %.1f ms)",
              (prepared - t0) * 1000, (started - prepared) * 1000, gpuAt * 1000, frames, total * 1000, worst * 1000);
    ok = ok && gpuAt >= 0;
    }
    DestroyWindow(probe);
    return ok;
}

} // namespace md
