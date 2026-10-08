#include "traffic_window.h"

#include <dwmapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <future>
#include <optional>

#include "../core/diag.h"
#include "../core/log.h"

namespace md {

TrafficWindow* TrafficWindow::self_ = nullptr;

namespace {
constexpr wchar_t kLightsClass[] = L"MacMenuBarLights";
constexpr UINT_PTR kSampleTimer = 1;   // couleur remesurée 200 ms après le dernier déplacement
constexpr UINT_PTR kProbeTimer = 2;    // boutons de Windows resondés une fois le redimensionnement calmé
constexpr UINT_PTR kBounceTimer = 3;   // rebond de la pastille relâchée
constexpr double kBounceMs = 240;
constexpr UINT_PTR kDeferTimer = 4;   // activation ou événement arrivé pendant une sonde
// Fenêtre qui retrouve sa barre de titre (sortie du plein écran) : l'app la redessine un peu après avoir pris sa
// nouvelle taille ; les pastilles attendent, pour ne pas flotter sur l'ancienne image.
constexpr UINT_PTR kRevealTimer = 5;
constexpr ULONGLONG kRevealMs = 120;
// Messages postés au fil des pastilles (sans fenêtre : les calques peuvent être recréés).
constexpr UINT kMsgAttach = WM_APP + 1, kMsgDetach = WM_APP + 2, kMsgQuit = WM_APP + 4, kMsgRecreate = WM_APP + 5,
               kMsgCapture = WM_APP + 6;


} // namespace

// DPI réel de l'écran de la fenêtre (une app non consciente du DPI répond 96 à GetDpiForWindow).
UINT effectiveDpi(HWND h) {
    UINT x = 96, y = 96;
    if (FAILED(GetDpiForMonitor(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &x, &y))) return 96;
    return x;
}

// Processus d'intégrité plus élevée que la nôtre (ou illisible) : UIPI refuserait nos messages.
static bool higherIntegrity(DWORD pid) {
    auto level = [](HANDLE process) -> std::optional<DWORD> {
        HANDLE token = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return std::nullopt;
        std::uint8_t buf[64] = {};
        DWORD size = 0;
        std::optional<DWORD> out;
        if (GetTokenInformation(token, TokenIntegrityLevel, buf, sizeof buf, &size)) {
            auto* tml = reinterpret_cast<TOKEN_MANDATORY_LABEL*>(buf);
            out = *GetSidSubAuthority(tml->Label.Sid, *GetSidSubAuthorityCount(tml->Label.Sid) - 1);
        }
        CloseHandle(token);
        return out;
    };
    const auto mine = level(GetCurrentProcess());
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return true;
    const auto theirs = level(p);
    CloseHandle(p);
    if (!theirs) return true;   // jeton illisible : un processus élevé
    return mine && *theirs > *mine;
}

// Pastille jaune enfoncée : le Dock prépare l'effet génie (capture de la fenêtre) pendant l'appui, comme il le fait pour
// le bouton « réduire » de Windows, que les pastilles cachent ; sinon la capture partirait au relâchement, en retard.
static void announceMinimize(HWND target, POINT p) {
    static const UINT msg = RegisterWindowMessageW(L"MacDockGenieArm");
    if (HWND dock = FindWindowW(L"MacDockWindow", nullptr))
        PostMessageW(dock, msg, reinterpret_cast<WPARAM>(target), MAKELPARAM(WORD(SHORT(p.x)), WORD(SHORT(p.y))));
}

LightsWindowInfo readInfo(HWND h) {
    LightsWindowInfo w;
    w.style = LONG(GetWindowLongPtrW(h, GWL_STYLE));
    w.exStyle = LONG(GetWindowLongPtrW(h, GWL_EXSTYLE));
    w.classStyle = UINT(GetClassLongPtrW(h, GCL_STYLE));
    wchar_t cls[128] = {};
    GetClassNameW(h, cls, 128);
    w.className = cls;
    if (FAILED(DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &w.frame, sizeof w.frame))) GetWindowRect(h, &w.frame);
    RECT c{};
    GetClientRect(h, &c);
    POINT o{0, 0};
    ClientToScreen(h, &o);
    w.client = RECT{o.x, o.y, o.x + c.right, o.y + c.bottom};
    w.zoomed = IsZoomed(h) != FALSE;
    w.iconic = IsIconic(h) != FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    w.ownProcess = pid == GetCurrentProcessId();
    w.elevated = !w.ownProcess && higherIntegrity(pid);
    TITLEBARINFO tb{sizeof tb};
    if (GetTitleBarInfo(h, &tb) && tb.rcTitleBar.bottom > tb.rcTitleBar.top) w.captionBottom = tb.rcTitleBar.bottom;
    return w;
}

bool TrafficWindow::create(HINSTANCE instance) {
    if (thread_.joinable()) return true;
    self_ = this;
    instance_ = instance;
    quitting_ = false;
    std::promise<bool> started;
    std::future<bool> result = started.get_future();
    thread_ = std::thread([this, &started] {
        threadId_ = GetCurrentThreadId();
        const bool created = ensureLayers();   // copie locale : la promesse n'existe plus après set_value
        started.set_value(created);
        if (created) run();
    });
    const bool ok = result.get();   // sans limite : le fil répond toujours (comme UiaWorker)
    if (!ok) {
        thread_.join();
        threadId_ = 0;
    }
    return ok;
}

void TrafficWindow::run() {
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!m.hwnd) {
            switch (m.message) {
                case kMsgAttach: doAttach(reinterpret_cast<HWND>(m.wParam), LightsMode(m.lParam)); continue;
                case kMsgDetach: doDetach(); continue;
                case kMsgRecreate:
                    if (ensureLayers() && target_) place(true);
                    continue;
                case kMsgCapture:
                    captureVisible_ = m.wParam != 0;
                    if (hwnd_) SetWindowDisplayAffinity(hwnd_, captureVisible_ || diagnosticCapture() ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE);
                    if (captureDone_) SetEvent(captureDone_);
                    continue;
                case kMsgQuit:
                    quitting_ = true;
                    doDetach();
                    destroyLayers();
                    PostQuitMessage(0);
                    continue;
                default: break;
            }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

void TrafficWindow::attach(HWND target, LightsMode mode) {
    if (threadId_ && !PostThreadMessageW(threadId_, kMsgAttach, reinterpret_cast<WPARAM>(target), LPARAM(mode)))
        log::warn(L"Feux tricolores : message perdu (%lu)", GetLastError());
}

void TrafficWindow::detach() {
    if (threadId_) PostThreadMessageW(threadId_, kMsgDetach, 0, 0);
}

void TrafficWindow::setCaptureVisible(bool on) {
    if (!threadId_) return;
    if (!captureDone_) captureDone_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!captureDone_) return;
    ResetEvent(captureDone_);
    if (PostThreadMessageW(threadId_, kMsgCapture, on ? 1 : 0, 0)) WaitForSingleObject(captureDone_, 200);
}

void TrafficWindow::destroy() {
    if (thread_.joinable()) {
        PostThreadMessageW(threadId_, kMsgQuit, 0, 0);
        // Borné : un glisser en cours vers une app figée ne doit pas bloquer la fermeture de la barre.
        if (WaitForSingleObject(thread_.native_handle(), 3000) == WAIT_OBJECT_0) thread_.join();
        else thread_.detach();
    }
    threadId_ = 0;
    if (self_ == this) self_ = nullptr;
    if (captureDone_) CloseHandle(captureDone_);
    captureDone_ = nullptr;
}

bool TrafficWindow::ensureLayers() {
    if (hwnd_ && IsWindow(hwnd_)) return true;   // chemin courant : rien à faire
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kLightsClass;
    RegisterClassExW(&wc);   // déjà inscrite : sans effet
    // La mesure de la couleur ne voit pas les calques (sauf en diagnostic : visibles aux enregistreurs d'écran ; la
    // mesure se fait à côté d'eux de toute façon).
    const DWORD affinity = diagnosticCapture() || captureVisible_ ? WDA_NONE : WDA_EXCLUDEFROMCAPTURE;
    hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kLightsClass, L"", WS_POPUP, 0, 0, 1, 1,
                            nullptr, nullptr, instance_, nullptr);
    if (!hwnd_) {
        log::warn(L"Feux tricolores : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    SetWindowDisplayAffinity(hwnd_, affinity);
    painted_ = false;
    paintedSize_ = {};
    return true;
}

void TrafficWindow::destroyLayers() {
    if (hwnd_) DestroyWindow(hwnd_);
    hwnd_ = nullptr;
}

void TrafficWindow::moveLayer(HWND layer, const RECT& r) {
    if (!layer) return;
    POINT pos{r.left, r.top};
    UpdateLayeredWindow(layer, nullptr, &pos, nullptr, nullptr, nullptr, 0, nullptr, 0);   // même image, déplacée
}

void TrafficWindow::unhook() {
    if (hook_) UnhookWinEvent(hook_);
    if (moveHook_) UnhookWinEvent(moveHook_);
    hook_ = moveHook_ = nullptr;
}

void TrafficWindow::doDetach() {
    unhook();
    target_ = nullptr;
    pressed_ = -1;
    dragging_ = false;
    state_.hover = false;
    state_.pressed = state_.bouncing = -1;
    placement_ = {};
    probeRetries_ = 0;
    refused_ = nullptr;
    revealAt_ = 0;
    if (hwnd_) KillTimer(hwnd_, kProbeTimer);
    hide();
    publicTarget_ = nullptr;
}

void TrafficWindow::hide() {
    if (hwnd_ && shown_) ShowWindow(hwnd_, SW_HIDE);
    shown_ = false;
    // La nouvelle sonde reste programmée : une fenêtre masquée faute de mesure complète (app qui démarre) doit
    // recevoir ses pastilles à la reprise. Seul detach() l'annule.
    if (hwnd_) KillTimer(hwnd_, kSampleTimer);
}

void TrafficWindow::doAttach(HWND target, LightsMode mode) {
    if (!ensureLayers()) return;
    if (probing_) {
        attachPending_ = true;
        pendingTarget_ = target;
        pendingMode_ = mode;
        SetTimer(hwnd_, kDeferTimer, 0, nullptr);
        return;
    }
    mode_ = mode;
    if (target != target_ || !hook_) {   // nouvelle cible, ou cible sans pastilles réévaluée (réglage changé)
        doDetach();
        if (!target || !IsWindow(target)) return;
        target_ = target;
        publicTarget_ = target;
        painted_ = false;
        LightsWindowInfo info = readInfo(target);
        // Encore réduite quand elle devient active (restauration depuis le Dock) : suivie quand même, ses pastilles
        // arrivent quand elle réapparaît (place() la masque tant qu'elle est réduite).
        info.iconic = false;
        if (mode == LightsMode::Off || !wantsLights(info, mode, effectiveDpi(target))) {
            if (diagnosticCapture())
                log::info(L"[diag] pastilles %p [%ls] : refusée (style %08lx, élevée %d)", target, info.className.c_str(),
                          info.style, int(info.elevated));
            hide();   // pas de crochet pour une fenêtre sans pastilles (bureau, fenêtres outils…)
            return;
        }
        DWORD pid = 0;
        GetWindowThreadProcessId(target, &pid);
        // Cible détruite, montrée, masquée ou réordonnée, puis déplacée : seulement les événements de son processus.
        const DWORD flags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;
        hook_ = SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_REORDER, nullptr, onEvent, pid, 0, flags);
        moveHook_ = SetWinEventHook(EVENT_OBJECT_LOCATIONCHANGE, EVENT_OBJECT_LOCATIONCHANGE, nullptr, onEvent, pid, 0, flags);
    }
    place(true);
    if (shown_) SetTimer(hwnd_, kSampleTimer, 300, nullptr);   // remesure une fois l'apparition de la fenêtre finie
}

void CALLBACK TrafficWindow::onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    TrafficWindow* t = self_;
    if (!t || !t->target_) return;
    if (t->probing_) {   // au milieu d'une sonde : replacé juste après
        t->placePending_ = true;
        SetTimer(t->hwnd_, kDeferTimer, 0, nullptr);
        return;
    }
    if (event == EVENT_OBJECT_REORDER) {   // ordre d'affichage changé dans ce processus : on reste au-dessus
        if (t->shown_) t->raise();
        return;
    }
    if (hwnd != t->target_ || idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
    switch (event) {
        case EVENT_OBJECT_DESTROY: t->doDetach(); break;
        case EVENT_OBJECT_HIDE: t->hide(); break;
        case EVENT_OBJECT_SHOW:
        case EVENT_OBJECT_LOCATIONCHANGE:
            t->place(false);
            if (t->shown_) SetTimer(t->hwnd_, kSampleTimer, 200, nullptr);
            break;
        default: break;
    }
}

void TrafficWindow::sample(const RECT& frame, UINT dpi) {
    // Ligne à mi-hauteur des boutons, juste à gauche du calque : la barre de titre que les boutons interrompent (Mica
    // en dégradé : la teinte de leur hauteur).
    const LONG y = (layout_.window.top + layout_.window.bottom) / 2;
    std::vector<std::uint32_t> samples;
    if (HDC dc = GetDC(nullptr)) {
        for (int i = 0; i < 9; ++i) {
            const LONG x = layout_.window.left - 4 - i * std::lround(3.0 * dpi / 96);
            if (x <= frame.left + 8) break;
            const COLORREF c = GetPixel(dc, x, y);
            if (c != CLR_INVALID) samples.push_back((GetRValue(c) << 16) | (GetGValue(c) << 8) | GetBValue(c));
        }
        ReleaseDC(nullptr, dc);
    }
    if (samples.empty()) return;
    const std::uint32_t color = dominantColor(samples);
    if (color == state_.patchColor && painted_) return;
    state_.patchColor = color;
    const double lum = 0.299 * ((color >> 16) & 0xFF) + 0.587 * ((color >> 8) & 0xFF) + 0.114 * (color & 0xFF);
    state_.dark = lum < 128;
    painted_ = false;
}

TrafficWindow::Placement TrafficWindow::measure(const LightsWindowInfo& info, UINT dpi, bool& complete) const {
    Placement p;
    p.target = target_;
    p.size = SIZE{info.frame.right - info.frame.left, info.frame.bottom - info.frame.top};
    p.zoomed = info.zoomed;
    p.dpi = dpi;
    p.valid = true;
    const HWND target = target_;
    // Sonde en lecture seule : WM_NCHITTEST ne fait que répondre une zone. 20 ms par appel au plus et 60 ms en
    // tout : une app occupée n'arrête pas la barre de menus (la mesure est alors reprise plus tard).
    const ULONGLONG deadline = GetTickCount64() + 60;
    bool late = false;
    const HitProbe hit = [&](POINT pt) -> LRESULT {
        if (late || GetTickCount64() > deadline) {
            late = true;
            return HTNOWHERE;
        }
        POINT q = pt;
        PhysicalToLogicalPointForPerMonitorDPI(target, &q);   // une app non consciente du DPI répond en points logiques
        DWORD_PTR r = HTNOWHERE;
        if (!SendMessageTimeoutW(target, WM_NCHITTEST, 0, MAKELPARAM(WORD(SHORT(q.x)), WORD(SHORT(q.y))),
                                 SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 20, &r)) {
            late = true;
            return HTNOWHERE;
        }
        return LRESULT(r);
    };
    RECT window{}, bounds{};
    GetWindowRect(target, &window);
    if (FAILED(DwmGetWindowAttribute(target, DWMWA_CAPTION_BUTTON_BOUNDS, &bounds, sizeof bounds))) bounds = {};
    const RECT b = captionButtons(window, info.frame, bounds, dpi, hit);
    const bool found = b.right > b.left && b.bottom > b.top;
    complete = !late;
    p.spot = found ? Spot::Over : Spot::None;   // sans boutons trouvés : pas de pastilles, l'app garde les siens
    if (found) p.buttons = RECT{b.left - info.frame.right, b.top - info.frame.top, b.right - info.frame.right, b.bottom - info.frame.top};
    return p;
}

void TrafficWindow::place(bool resample, bool probe) {
    if (!target_) return;
    if (!IsWindow(target_)) {
        doDetach();
        return;
    }
    if (!ensureLayers()) return;
    LightsWindowInfo info = readInfo(target_);
    MONITORINFO mon{sizeof mon};
    if (GetMonitorInfoW(MonitorFromWindow(target_, MONITOR_DEFAULTTONEAREST), &mon))
        info.frame = visibleFrame(info.frame, mon.rcWork, info.zoomed);   // agrandie : pas sous la barre de menus
    const UINT dpi = effectiveDpi(target_);
    if (!IsWindowVisible(target_) || !wantsLights(info, mode_, dpi)) {
        if (diagnosticCapture()) log::info(L"[diag] pastilles %p : refusée (style %08lx)", target_, info.style);
        if (IsWindowVisible(target_) && !info.iconic) refused_ = target_;   // plein écran : sans barre de titre
        hide();
        return;
    }
    const RECT& f = info.frame;
    const SIZE size{f.right - f.left, f.bottom - f.top};
    if (probe || !placement_.valid || placement_.target != target_ || placement_.zoomed != info.zoomed ||
        placement_.dpi != dpi) {
        bool complete = true;
        const ULONGLONG t0 = GetTickCount64();
        const HWND probed = target_;
        probing_ = true;
        Placement p = measure(info, dpi, complete);
        probing_ = false;
        if (target_ != probed) return;   // par sécurité : la cible ne change qu'après la sonde (kDeferTimer)
        if (diagnosticCapture())
            log::info(L"[diag] pastilles %p : place %d, boutons %ld,%ld,%ld,%ld, complet %d, %llu ms", target_, int(p.spot),
                      p.buttons.left, p.buttons.top, p.buttons.right, p.buttons.bottom, int(complete), GetTickCount64() - t0);
        if (complete) {
            probeRetries_ = 0;
            placement_ = p;
        } else {
            // Sonde interrompue (app occupée) : réponses manquantes, la place déduite serait fausse. On garde la mesure
            // précédente de cette fenêtre, sinon rien jusqu'à la reprise.
            if (!(placement_.valid && placement_.target == target_)) {
                placement_ = p;
                placement_.spot = Spot::None;
            }
            if (probeRetries_++ < 3) SetTimer(hwnd_, kProbeTimer, 500, nullptr);
        }
    } else if (size.cx != placement_.size.cx || size.cy != placement_.size.cy) {
        // Redimensionnement : les boutons restent ancrés à droite ; nouvelle sonde une fois le geste calmé.
        SetTimer(hwnd_, kProbeTimer, 150, nullptr);
    }
    spot_ = placement_.spot;
    const RECT& pb = placement_.buttons;
    const bool hasButtons = pb.right > pb.left;
    const RECT buttons{f.right + pb.left, f.top + pb.top, f.right + pb.right, f.top + pb.bottom};
    if (spot_ != Spot::Over || !hasButtons) {
        hide();
        return;
    }
    if (refused_ == target_) {   // la barre de titre revient (sortie du plein écran) : un instant de patience
        refused_ = nullptr;
        revealAt_ = GetTickCount64() + kRevealMs;
        SetTimer(hwnd_, kRevealTimer, UINT(kRevealMs), nullptr);
    }
    if (GetTickCount64() < revealAt_) return;   // montrées par kRevealTimer
    const LightsLayout l = lightsOverButtons(buttons, dpi, info.zoomed);
    const bool resized = l.window.right - l.window.left != layout_.window.right - layout_.window.left ||
                         l.window.bottom - l.window.top != layout_.window.bottom - layout_.window.top ||
                         l.topGap != layout_.topGap;
    layout_ = l;
    scale_ = dpi / 96.0;
    // Fermer : menu système, ou bouton fermer trouvé par la sonde (Electron sans menu système).
    const bool closable = !(info.classStyle & CS_NOCLOSE) && ((info.style & WS_SYSMENU) || hasButtons);
    const bool enabled[3] = {closable, (info.style & WS_MINIMIZEBOX) != 0, (info.style & WS_MAXIMIZEBOX) != 0};
    for (int i = 0; i < 3; ++i)
        if (state_.enabled[i] != enabled[i]) {
            state_.enabled[i] = enabled[i];
            painted_ = false;
        }
    if (resample || !painted_) sample(info.frame, dpi);
    if (resized) painted_ = false;
    if (!painted_) paint();
    else moveLayer(hwnd_, layout_.window);
    raise();
}

void TrafficWindow::paint() {
    paintedSize_ = {};
    paintLayer(hwnd_, layout_, paintedSize_, state_.patchColor);
    painted_ = paintedSize_.cx > 0;
}

void TrafficWindow::paintLayer(HWND layer, const LightsLayout& layout, SIZE& painted, std::uint32_t patchColor) {
    const int w = int(layout.window.right - layout.window.left), h = int(layout.window.bottom - layout.window.top);
    if (!layer || w <= 0 || h <= 0) return;
    LightsState st = state_;
    st.patchColor = patchColor;
    const auto px = renderLights(layout, st, scale_);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (bmp && bits) {
        std::memcpy(bits, px.data(), px.size());
        HGDIOBJ old = SelectObject(mem, bmp);
        POINT pos{layout.window.left, layout.window.top}, zero{0, 0};
        SIZE size{w, h};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(layer, screen, &pos, &size, mem, &zero, 0, &blend, ULW_ALPHA);
        SelectObject(mem, old);
        painted = size;
    }
    if (bmp) DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

void TrafficWindow::raise() {
    if (!hwnd_ || !target_) return;
    // Ordre voulu : les pastilles, juste au-dessus de la cible.
    const HWND above = GetWindow(target_, GW_HWNDPREV);
    const bool inOrder = above == hwnd_;
    UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW;
    HWND after = HWND_TOP;   // la cible est au premier plan : le haut des fenêtres ordinaires
    if (inOrder) {
        flags |= SWP_NOZORDER;   // déjà juste au-dessus
    } else {
        HWND a = above;
        while (a && a == hwnd_) a = GetWindow(a, GW_HWNDPREV);
        if (a && !(GetWindowLongPtrW(a, GWL_EXSTYLE) & WS_EX_TOPMOST)) after = a;
    }
    SetWindowPos(hwnd_, after, 0, 0, 0, 0, flags);
    shown_ = true;
}

LRESULT CALLBACK TrafficWindow::proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (self_ && hwnd && self_->hwnd_ == hwnd) return self_->handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrafficWindow::handle(HWND from, UINT msg, WPARAM wp, LPARAM lp) {
    const auto screenPoint = [] {   // position écran du message : juste même si le calque a bougé depuis
        const DWORD pos = GetMessagePos();
        return POINT{short(LOWORD(pos)), short(HIWORD(pos))};
    };
    const auto overGroup = [&](POINT p) {
        return p.x >= layout_.circles[0].left - 2 && p.x <= layout_.circles[2].right + 2 && p.y >= layout_.circles[0].top - 2 &&
               p.y <= layout_.circles[0].bottom + 2;
    };
    switch (msg) {
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_NCDESTROY:   // détruit sans nous (ne devrait pas arriver) : recréé sur ce fil
            if (!quitting_) {
                if (from == hwnd_) hwnd_ = nullptr;
                shown_ = false;
                tracking_ = false;
                PostThreadMessageW(GetCurrentThreadId(), kMsgRecreate, 0, 0);
            }
            break;   // la fenêtre de l'app reste active
        case WM_MOUSEMOVE: {
            if (dragging_ && target_) {
                const POINT p = screenPoint();
                SetWindowPos(target_, nullptr, dragFrom_.left + p.x - dragStart_.x, dragFrom_.top + p.y - dragStart_.y, 0, 0,
                             SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
                return 0;
            }
            if (!tracking_) {
                TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
                tracking_ = TrackMouseEvent(&t) != FALSE;
            }
            const POINT p = screenPoint();
            const bool hover = overGroup(p);
            // Pastille appuyée : enfoncée seulement tant que le doigt reste dessus, comme sur macOS.
            const int pressed = pressed_ >= 0 && hitLight(layout_, p) == pressed_ ? pressed_ : -1;
            if (hover != state_.hover || pressed != state_.pressed) {
                state_.hover = hover;   // symboles ×, −, + sur les trois, comme sur macOS
                state_.pressed = pressed;
                paint();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            tracking_ = false;
            if (state_.hover) {
                state_.hover = false;
                paint();
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK: {
            const POINT p = screenPoint();
            const int hit = hitLight(layout_, p);
            pressed_ = -1;
            switch (lightsMouse(msg == WM_LBUTTONDBLCLK, hit, state_.enabled)) {
                case LightsMouse::Press:
                    pressed_ = hit;
                    state_.pressed = hit;
                    state_.bouncing = -1;
                    SetCapture(hwnd_);
                    paint();
                    if (hit == 1 && target_) announceMinimize(target_, p);   // le génie se prépare avant le relâchement
                    break;
                case LightsMouse::Drag:   // le fond appartient à la barre de titre : on déplace la fenêtre nous-mêmes
                    if (target_ && !IsZoomed(target_) && GetWindowRect(target_, &dragFrom_)) {
                        dragging_ = true;
                        dragStart_ = p;
                        SetCapture(from);
                    }
                    break;
                case LightsMouse::Zoom:
                    if (target_)
                        if (UINT cmd = captionDoubleClick((GetWindowLongPtrW(target_, GWL_STYLE) & WS_MAXIMIZEBOX) != 0,
                                                          IsZoomed(target_) != FALSE))
                            PostMessageW(target_, WM_SYSCOMMAND, cmd, 0);
                    break;
                case LightsMouse::None: break;
            }
            return 0;
        }
        case WM_CAPTURECHANGED:
            pressed_ = -1;
            dragging_ = false;
            if (state_.pressed >= 0) {
                state_.pressed = -1;
                paint();
            }
            return 0;
        case WM_LBUTTONUP: {
            if (dragging_) {
                dragging_ = false;
                ReleaseCapture();
                return 0;
            }
            if (pressed_ < 0) return 0;
            const int pressed = pressed_;
            pressed_ = -1;
            ReleaseCapture();
            // Petit rebond élastique de la pastille relâchée (Golden Gate), puis la commande.
            state_.pressed = -1;
            state_.bouncing = pressed;
            state_.bounce = 1 - 0.06;
            bounceStart_ = GetTickCount64();
            SetTimer(hwnd_, kBounceTimer, 15, nullptr);
            paint();
            if (hitLight(layout_, screenPoint()) == pressed && target_ && IsWindow(target_))
                PostMessageW(target_, WM_SYSCOMMAND, lightCommand(pressed, IsZoomed(target_) != FALSE), 0);
            return 0;
        }
        case WM_TIMER:
            if (wp == kDeferTimer) {
                KillTimer(hwnd_, kDeferTimer);
                if (attachPending_) {
                    attachPending_ = placePending_ = false;
                    doAttach(pendingTarget_, pendingMode_);
                } else if (placePending_) {
                    placePending_ = false;
                    if (target_ && !IsWindow(target_)) doDetach();
                    else place(false);
                }
            } else if (wp == kRevealTimer) {
                KillTimer(hwnd_, kRevealTimer);
                revealAt_ = 0;
                place(true, true);   // nouvelle sonde : les boutons ont pu changer de taille
            } else if (wp == kSampleTimer) {
                KillTimer(hwnd_, kSampleTimer);
                place(true);
            } else if (wp == kProbeTimer) {
                KillTimer(hwnd_, kProbeTimer);
                place(true, true);
            } else if (wp == kBounceTimer) {
                const double t = double(GetTickCount64() - bounceStart_) / kBounceMs;
                if (t >= 1 || state_.bouncing < 0) {
                    KillTimer(hwnd_, kBounceTimer);
                    state_.bouncing = -1;
                    state_.bounce = 1;
                } else {   // ressort amorti : 0,94 → léger dépassement → 1
                    state_.bounce = 1 - 0.06 * std::cos(2.4 * 3.14159265358979 * t) * std::exp(-3.5 * t);
                }
                if (shown_) paint();
            }
            return 0;
        default: break;
    }
    return DefWindowProcW(from, msg, wp, lp);
}

} // namespace md
