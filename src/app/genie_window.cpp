#include "genie_window.h"

#include <algorithm>

#include "../core/diag.h"
#include "../core/log.h"
#include "window_capture.h"
#include "thumbnails.h"

namespace md {

namespace {
constexpr wchar_t kGenieClass[] = L"MacDockGenie";

RECT monitorOf(const RECT& r) {
    MONITORINFO mi{sizeof mi};
    if (!GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) return r;
    return mi.rcMonitor;
}
} // namespace

void GenieWindow::arm(HINSTANCE instance, HWND source, const RECT& visible, const RECT& dock, POINT down) {
    if (running_ || active()) return;   // une animation garde sa fenêtre
    disarm();
    if (!ensureWindow(instance)) return;
    HTHUMBNAIL t = nullptr;
    if (FAILED(DwmRegisterThumbnail(hwnd_, source, &t))) return;
    {
        std::lock_guard lock(coverLock_);
        armedThumb_ = t;
        armDown_ = down;
        armedShown_ = false;
    }
    armedSource_ = source;
    armedBox_ = genieHostBox(monitorOf(visible), monitorOf(dock));
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_VISIBLE | DWM_TNP_RECTDESTINATION | DWM_TNP_OPACITY | DWM_TNP_SOURCECLIENTAREAONLY;
    p.fVisible = FALSE;
    p.opacity = 255;
    p.fSourceClientAreaOnly = FALSE;
    p.rcDestination = visible;
    OffsetRect(&p.rcDestination, -armedBox_.left, -armedBox_.top);
    DwmUpdateThumbnailProperties(t, &p);
    // Vide et traversée par les clics : rien ne se voit tant que la couverture est invisible.
    SetWindowPos(hwnd_, HWND_TOPMOST, armedBox_.left, armedBox_.top, armedBox_.right - armedBox_.left,
                 armedBox_.bottom - armedBox_.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    // Surface GPU à sa taille finale (la fenêtre et tout le Dock : la case d'arrivée y est), affichée transparente.
    armedVisible_ = visible;
    armedGpuBox_ = genieGpuBox(visible, dock, nullptr);
    gpu_.warm(instance, source, &armedGpuBox_);
}

void GenieWindow::pumpArmed() {
    if (!armedThumb_ || armedShown_) return;   // relâché : Windows retire la fenêtre, la capture ne la montre plus
    // Maillage plat sur la partie visible : le tracé à blanc passe par les mêmes textures et shaders que l'animation.
    const auto& v = armedVisible_;
    const std::vector<GenieVertex> flat{{float(v.left), float(v.top), 0, 0},
                                        {float(v.right), float(v.top), 1, 0},
                                        {float(v.left), float(v.bottom), 0, 1},
                                        {float(v.right), float(v.bottom), 1, 1}};
    gpu_.pump(flat);
}

bool GenieWindow::revealArmed(POINT up) {
    std::lock_guard lock(coverLock_);
    if (!armedThumb_) return false;
    if (armedShown_) return true;
    if (!genieMinimizeConfirmed(armDown_, up)) return false;
    DWM_THUMBNAIL_PROPERTIES p{};
    p.dwFlags = DWM_TNP_VISIBLE;
    p.fVisible = TRUE;
    DwmUpdateThumbnailProperties(armedThumb_, &p);
    armedShown_ = true;
    return true;
}

void GenieWindow::disarm() {
    if (!armedThumb_) return;
    {
        std::lock_guard lock(coverLock_);
        DwmUnregisterThumbnail(armedThumb_);
        armedThumb_ = nullptr;
    }
    armedSource_ = nullptr;
    if (!active() && hwnd_) ShowWindow(hwnd_, SW_HIDE);
    if (!running_) gpu_.cool();
}

void GenieWindow::warm(HINSTANCE instance, HWND source) {
    if (!running_ && !active() && !armedThumb_) gpu_.warm(instance, source);
}

void GenieWindow::cool() {
    if (!running_ && !active() && !armedThumb_) gpu_.cool();
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
    LARGE_INTEGER q0, q1, q2, q3, qf;
    QueryPerformanceCounter(&q0);
    // Réduction annoncée : sa couverture (peut-être déjà affichée) et sa capture sont reprises telles quelles.
    HTHUMBNAIL first = nullptr;
    RECT armedBox{}, armedGpuBox{};
    if (armedThumb_ && armedSource_ == source && !running_ && !active()) {
        armedBox = armedBox_;
        armedGpuBox = armedGpuBox_;
        std::lock_guard lock(coverLock_);
        first = armedThumb_;
        armedThumb_ = nullptr;
        armedSource_ = nullptr;
    } else {
        disarm();
        if (running_ || active()) finish();   // sinon la capture lancée d'avance (case survolée) est gardée
    }
    if (effect == MinimizeEffect::Windows || !IsWindow(source) || !ensureWindow(instance)) {
        if (first) DwmUnregisterThumbnail(first);
        return false;
    }
    QueryPerformanceCounter(&q1);
    if (!first && FAILED(DwmRegisterThumbnail(hwnd_, source, &first))) return false;
    thumbs_.push_back(first);
    if (FAILED(DwmQueryThumbnailSourceSize(first, &src_)) || src_.cx <= 0 || src_.cy <= 0) {
        finish();
        return false;
    }
    source_ = source;
    from_ = genieVisibleRect(from, src_);   // la miniature n'a que la partie visible : pas d'image étirée ni décalée
    to_ = fitThumbnail(src_.cx, src_.cy, toCell);   // la forme de la miniature dans sa case
    edge_ = edge;
    effect_ = effect;
    restore_ = restore;
    box_ = genieHostBox(monitorOf(from_), monitorOf(to_));
    if (!IsRectEmpty(&armedBox) && !EqualRect(&armedBox, &box_))
        log::info(L"Génie : cadre annoncé différent, la fenêtre des bandes se déplace");
    const RECT gpuBox = genieGpuBox(from_, to_, &armedGpuBox);   // celle posée à l'appui si elle suffit
    const LONG extent = edge == DockPosition::Bottom ? from.bottom - from.top : from.right - from.left;
    fullSlices_ = effect == MinimizeEffect::Scale ? 1 : genieSliceCount(extent);
    rows_ = effect == MinimizeEffect::Scale ? 1 : std::clamp(int(extent / 3), 48, 256);   // maillage GPU : une rangée / 3 px
    slices_ = genieStripTarget(fullSlices_, true, 0);   // une : la fenêtre n'est pas encore déformée
    start_ = begun_ = now;
    elapsed_ = 0;
    duration_ = minimizeDuration(effect, slow);
    running_ = true;
    stripsHidden_ = false;
    gpuStarted_ = false;
    // Couverture d'abord : Windows a déjà retiré la fenêtre réduite de l'écran ; chaque milliseconde ici est un trou.
    QueryPerformanceCounter(&q2);
    placeStrips(restore ? 1.0 : 0.0);
    if (!EqualRect(&armedBox, &box_))   // annoncée : déjà affichée à cette place
        SetWindowPos(hwnd_, HWND_TOPMOST, box_.left, box_.top, box_.right - box_.left, box_.bottom - box_.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
    LARGE_INTEGER q3b;
    QueryPerformanceCounter(&q3b);
    // Puis le rendu GPU (capture sur un fil) ; les bandes de secours s'ajoutent pendant l'attente (step).
    gpuStarted_ = gpu_.begin(instance, source, gpuBox);
    waiting_ = gpuStarted_;   // l'horloge part avec la première image GPU (step)
    // Capture prise d'avance : première image tout de suite (la passation en demande deux, la suivante au tour d'après).
    if (gpuStarted_) gpu_.frame(genieMesh(effect_, src_, from_, to_, edge_, restore ? 1.0 : 0.0, rows_));
    QueryPerformanceCounter(&q3);
    if (diagnosticCapture()) {
        LARGE_INTEGER q4;
        QueryPerformanceCounter(&q4);
        QueryPerformanceFrequency(&qf);
        auto ms = [&](LARGE_INTEGER a, LARGE_INTEGER b) { return double(b.QuadPart - a.QuadPart) * 1000.0 / double(qf.QuadPart); };
        log::info(L"[diag] génie : départ %.1f ms (fenêtre %.1f, miniature %.1f, couverture %.1f, GPU %.1f) ; "
                  L"qpc couverture %.1f ms", ms(q0, q4), ms(q0, q1), ms(q1, q2), ms(q2, q3b), ms(q3b, q3),
                  double(q3b.QuadPart) * 1000.0 / double(qf.QuadPart));
    }
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
    placeStrips(t);
}

void GenieWindow::placeStrips(double t) {
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
    const int want = genieStripTarget(fullSlices_, waiting_, int(thumbs_.size()));
    while (int(thumbs_.size()) < want) {
        HTHUMBNAIL t = nullptr;
        if (FAILED(DwmRegisterThumbnail(hwnd_, source_, &t))) break;
        thumbs_.push_back(t);
    }
    slices_ = int(thumbs_.size());
}

bool GenieWindow::step(double now) {
    if (!running_) return false;
    if (!IsWindow(source_)) {   // fenêtre fermée pendant l'animation
        finish();
        return false;
    }
    if (waiting_) {
        const double t0 = restore_ ? 1.0 : 0.0;
        // Passation à l'instant de départ, où GPU et miniature montrent la même image : le mouvement ne part qu'une
        // fois le GPU à l'écran (deux images présentées) et la miniature retirée.
        const bool ready = gpu_.frame(genieMesh(effect_, src_, from_, to_, edge_, t0, rows_)) && gpu_.framesShown() >= 2;
        switch (genieWaitStep(ready, now - begun_)) {
            case GenieWait::Hold: return true;   // une miniature tient la fenêtre à sa place
            case GenieWait::GoStrips: gpuStarted_ = false; break;   // la capture n'arrive pas : bandes complètes
            case GenieWait::Go:
                ShowWindow(hwnd_, SW_HIDE);
                stripsHidden_ = true;
                break;
        }
        if (diagnosticCapture())
            log::info(L"[diag] génie : %s après %.1f ms d'attente", ready ? L"GPU prêt" : L"bandes (GPU en retard)",
                      (now - begun_) * 1000.0);
        waiting_ = false;
        start_ = now;
    }
    elapsed_ = now - start_;
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

bool genieFrameProbe(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = instance;
    wc.lpszClassName = L"MacDockFrameProbe";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
    RegisterClassExW(&wc);
    const int x = GetSystemMetrics(SM_XVIRTUALSCREEN) - 3000, y = GetSystemMetrics(SM_YVIRTUALSCREEN) + 100;
    HWND w = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName, L"Sonde", WS_OVERLAPPEDWINDOW, x, y, 800, 600, nullptr, nullptr,
                             instance, nullptr);
    if (!w) return false;
    ShowWindow(w, SW_SHOWNOACTIVATE);
    for (int i = 0; i < 20; ++i) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
        Sleep(10);
    }
    RECT window{}, visible{};
    GetWindowRect(w, &window);
    DwmGetWindowAttribute(w, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof visible);
    ShowWindow(w, SW_SHOWMINNOACTIVE);
    HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP, wc.lpszClassName, L"", WS_POPUP, x, y, 1, 1, nullptr,
                                nullptr, instance, nullptr);
    HTHUMBNAIL t = nullptr;
    SIZE thumb{};
    if (host && SUCCEEDED(DwmRegisterThumbnail(host, w, &t))) {
        DwmQueryThumbnailSourceSize(t, &thumb);
        DwmUnregisterThumbnail(t);
    }
    const RECT start = genieVisibleRect(window, thumb);
    const bool ok = EqualRect(&start, &visible) != FALSE;
    log::info(L"Sonde du cadre : fenêtre %ldx%ld, visible %ld,%ld %ldx%ld, miniature %ldx%ld, départ %ld,%ld %ldx%ld : %s",
              window.right - window.left, window.bottom - window.top, visible.left - window.left, visible.top - window.top,
              visible.right - visible.left, visible.bottom - visible.top, thumb.cx, thumb.cy, start.left - window.left,
              start.top - window.top, start.right - start.left, start.bottom - start.top, ok ? L"exact" : L"ÉCART");
    if (host) DestroyWindow(host);
    DestroyWindow(w);
    return ok;
}

} // namespace md
