#include "quicklook_window.h"

#include <dwmapi.h>
#include <mfapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <uiautomation.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <future>
#include <thread>

#include "../core/diag.h"
#include "../core/log.h"
#include "quicklook_logic.h"

namespace md {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"MacDockQuickLook";
constexpr UINT WM_APP_LOADED = WM_APP + 1;   // lParam : Content* préparé sur le fil de chargement
constexpr float kToolbar = 44, kClose = 22, kButtonH = 26, kPad = 12;   // points
constexpr std::size_t kTextLimit = 256 * 1024;

struct Palette {
    D2D1_COLOR_F toolbar, background, paper, ink, secondary, separator, button, buttonHot;
};

D2D1_COLOR_F hex(std::uint32_t c, float a = 1) {
    return D2D1::ColorF(((c >> 16) & 0xFF) / 255.f, ((c >> 8) & 0xFF) / 255.f, (c & 0xFF) / 255.f, a);
}

Palette paletteFor(bool dark) {
    if (dark)
        return {hex(0x2C2C2E), hex(0x1C1C1E), hex(0x1E1E1E), hex(0xF5F5F7), hex(0x98989D), hex(0x3A3A3C), hex(0x3A3A3C),
                hex(0x48484A)};
    return {hex(0xECECEE), hex(0xF5F5F7), hex(0xFFFFFF), hex(0x1D1D1F), hex(0x6E6E73), hex(0xD1D1D6), hex(0xFFFFFF),
            hex(0xE5E5EA)};
}

bool appsDark() {
    DWORD value = 1, size = sizeof value;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                 L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value == 0;
}

std::wstring fileNameOf(const std::wstring& path) {
    const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

// Pixels BGRA (haut en premier) d'un HBITMAP 32 bits ; une miniature sans alpha devient opaque.
bool pixelsOf(HBITMAP bmp, std::vector<std::uint8_t>& out, SIZE& size) {
    BITMAP bm{};
    if (!GetObjectW(bmp, sizeof bm, &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return false;
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), bm.bmWidth, -bm.bmHeight, 1, 32, BI_RGB};
    out.assign(std::size_t(bm.bmWidth) * bm.bmHeight * 4, 0);
    HDC dc = GetDC(nullptr);
    const int lines = GetDIBits(dc, bmp, 0, UINT(bm.bmHeight), out.data(), &bi, DIB_RGB_COLORS);
    ReleaseDC(nullptr, dc);
    if (lines != bm.bmHeight) {
        out.clear();
        return false;
    }
    bool anyAlpha = false;
    for (std::size_t i = 3; i < out.size(); i += 4) anyAlpha = anyAlpha || out[i] != 0;
    if (!anyAlpha)
        for (std::size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    size = SIZE{bm.bmWidth, bm.bmHeight};
    return true;
}

std::wstring dateLabel(const FILETIME& ft) {
    FILETIME local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return {};
    wchar_t day[64] = {}, time[32] = {};
    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &st, L"d MMM yyyy", day, 64, nullptr);
    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &st, nullptr, time, 32);
    return std::wstring(L"Modifié le ") + day + L" à " + time;
}

// Préparé hors du fil de l'interface : la miniature d'une vidéo ou d'une grande photo peut prendre du temps.
template <class Cancelled>
std::unique_ptr<QuickLookWindow::Content> loadContent(const std::wstring& path, float scale, Cancelled cancelled) {
    auto c = std::make_unique<QuickLookWindow::Content>();
    c->name = fileNameOf(path);
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    const bool exists = GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) != FALSE;
    const bool folder = exists && (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof sfi, SHGFI_TYPENAME)) c->kindName = sfi.szTypeName;
    if (exists) {
        const std::uint64_t size = (std::uint64_t(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
        c->details = folder ? dateLabel(fa.ftLastWriteTime) : quickLookSize(size) + L" — " + dateLabel(fa.ftLastWriteTime);
    }
    if (!folder) {   // vrai aperçu possible : vidéo ou son, ou document confié au gestionnaire d'aperçu du Shell
        c->media = quickLookMedia(path);
        c->shellPreview = quickLookUsesShellPreview(path) && previewHandlerFor(path, c->previewClsid);
    }
    c->openWith = L"Ouvrir";
    if (!folder) {
        const auto dot = path.find_last_of(L'.');
        wchar_t app[128] = {};
        DWORD n = 128;
        if (dot != std::wstring::npos &&
            SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_IGNOREUNKNOWN, ASSOCSTR_FRIENDLYAPPNAME, path.substr(dot).c_str(), nullptr,
                                        app, &n)) && *app)
            c->openWith = std::wstring(L"Ouvrir avec ") + app;
    }
    if (!folder && quickLookIsText(path)) {
        std::ifstream in(path, std::ios::binary);
        std::vector<std::uint8_t> bytes(kTextLimit);
        in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(bytes.size()));
        bytes.resize(std::size_t(in.gcount()));
        if (bytes.size() == kTextLimit) quickLookTrimUtf8(bytes);   // coupé : pas au milieu d'un caractère
        c->text = quickLookDecode(bytes);
        c->kind = QuickLookWindow::Content::Kind::Text;
        return c;
    }
    ComPtr<IShellItemImageFactory> factory;
    if (cancelled()) return c;   // sélection changée entre-temps : pas d'extraction de miniature pour rien
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) {
        HBITMAP bmp = nullptr;
        // Vraie image d'abord (photo, vidéo, PDF, document) ; à défaut, la grande icône du type de fichier.
        if (!folder && SUCCEEDED(factory->GetImage(SIZE{1600, 1600},
                                                   SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT, &bmp)) &&
            bmp && pixelsOf(bmp, c->pixels, c->size)) {
            c->kind = QuickLookWindow::Content::Kind::Image;
        } else {
            if (bmp) DeleteObject(bmp);
            bmp = nullptr;
            const LONG side = LONG(std::lround(128 * scale));
            if (SUCCEEDED(factory->GetImage(SIZE{side, side}, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &bmp)) && bmp)
                pixelsOf(bmp, c->pixels, c->size);
            c->kind = QuickLookWindow::Content::Kind::Icon;
        }
        if (bmp) DeleteObject(bmp);
    }
    return c;
}

std::wstring firstFont(IDWriteFactory* dw, std::initializer_list<const wchar_t*> names) {
    ComPtr<IDWriteFontCollection> fonts;
    if (dw && SUCCEEDED(dw->GetSystemFontCollection(&fonts, FALSE)))
        for (const wchar_t* n : names) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(fonts->FindFamilyName(n, &index, &exists)) && exists) return n;
        }
    return L"Segoe UI";
}

// UI Automation : rectangle de l'élément qui a le focus (le fichier sélectionné dans l'Explorateur), vide sinon.
RECT focusedItemRect() {
    ComPtr<IUIAutomation> uia;
    ComPtr<IUIAutomationElement> el;
    RECT r{};
    if (SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia))) &&
        SUCCEEDED(uia->GetFocusedElement(&el)) && el)
        el->get_CurrentBoundingRectangle(&r);
    return r;
}

constexpr UINT WM_APP_SHOW = WM_APP + 2;    // lParam : ShowRequest* (fil du Dock → fil de la fenêtre)
constexpr UINT WM_APP_CLOSE = WM_APP + 3;
constexpr UINT_PTR kAnimTimer = 1;
constexpr double kAnimSeconds = 0.22;

} // namespace

// ---- Fil du Dock ----

QuickLookWindow::~QuickLookWindow() {
    if (!ui_.joinable()) return;
    PostMessageW(hwnd_, WM_CLOSE, 0, 0);   // le fil sort de sa boucle, ferme ses aperçus et sa fenêtre
    // Borné : un gestionnaire d'aperçu figé ne doit pas bloquer l'arrêt du Dock.
    if (WaitForSingleObject(ui_.native_handle(), 3000) == WAIT_OBJECT_0) ui_.join();
    else ui_.detach();
}

bool QuickLookWindow::startThread(HINSTANCE instance) {
    if (ui_.joinable()) return hwnd_ != nullptr;
    std::promise<bool> ready;
    std::future<bool> result = ready.get_future();
    ui_ = std::thread(&QuickLookWindow::threadMain, this, instance, &ready);
    if (!result.get()) {   // la fenêtre est créée (ou non) avant de rendre la main : hwnd_ est lisible
        ui_.join();
        return false;
    }
    return true;
}

void QuickLookWindow::show(HINSTANCE instance, std::vector<std::wstring> paths, std::size_t index, HWND owner) {
    if (paths.empty() || !startThread(instance)) return;
    paths_ = std::move(paths);
    owner_ = owner;
    open_ = true;
    auto* r = new ShowRequest{paths_[std::min(index, paths_.size() - 1)], owner};
    if (!PostMessageW(hwnd_, WM_APP_SHOW, 0, reinterpret_cast<LPARAM>(r))) {
        delete r;
        open_ = false;
    }
}

void QuickLookWindow::close() {
    open_ = false;
    ++generation_;   // un chargement en cours sera ignoré
    if (hwnd_) PostMessageW(hwnd_, WM_APP_CLOSE, 0, 0);
}

// ---- Fil de la fenêtre ----

void QuickLookWindow::threadMain(HINSTANCE instance, std::promise<bool>* ready) {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
    instance_ = instance;
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // Flottante (au-dessus de l'Explorateur actif, qu'une fenêtre sans activation ne pourrait pas passer).
    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"Coup d'œil", WS_POPUP | WS_CLIPCHILDREN, 0, 0,
                            10, 10, nullptr, nullptr, instance, this);
    if (hwnd_) {
        const DWORD round = 2;   // DWMWCP_ROUND
        DwmSetWindowAttribute(hwnd_, 33, &round, sizeof round);
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    } else {
        log::warn(L"Coup d'œil : fenêtre impossible (%lu)", GetLastError());
    }
    const bool ok = hwnd_ && d2d_ && dwrite_;
    ready->set_value(ok);   // la promesse n'existe plus après
    if (ok) {
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    stopHosts();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();   // avant la fenêtre : le fil de chargement y poste ses résultats
    if (hwnd_) DestroyWindow(hwnd_);
    rt_.Reset();
    bitmap_.Reset();
    if (SUCCEEDED(mf)) MFShutdown();
    if (SUCCEEDED(co)) CoUninitialize();
}

void QuickLookWindow::onShow(const ShowRequest& r) {
    if (r.path != path_) stopHosts();   // le son d'une vidéo ne continue jamais sur le fichier suivant
    path_ = r.path;
    ownerUi_ = r.owner;
    dark_ = appsDark();
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(MonitorFromWindow(ownerUi_ ? ownerUi_ : hwnd_, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
    scale_ = dx / 96.f;
    if (diagnosticCapture()) log::info(L"[diag] coup d'œil : %s (fenêtre %p)", path_.c_str(), static_cast<void*>(hwnd_));
    startLoad();
}

void QuickLookWindow::onClose() {
    stopHosts();
    KillTimer(hwnd_, kAnimTimer);
    animating_ = false;
    ShowWindow(hwnd_, SW_HIDE);
    visible_ = fullscreen_ = false;
    content_.reset();
    bitmap_.Reset();
    path_.clear();
    hover_ = 0;
}

void QuickLookWindow::startLoad() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Request{path_, ++generation_, scale_, hwnd_};   // remplace une demande pas encore prise
    }
    wake_.notify_one();
    if (!worker_.joinable()) worker_ = std::thread(&QuickLookWindow::workerLoop, this);
}

void QuickLookWindow::workerLoop() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    for (;;) {
        Request req;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stop_ || pending_.has_value(); });
            if (stop_) break;
            req = std::move(*pending_);
            pending_.reset();
        }
        if (req.generation != generation_.load()) continue;   // déjà dépassée
        auto c = loadContent(req.path, req.scale, [&] { return req.generation != generation_.load(); });
        c->generation = req.generation;
        if (PostMessageW(req.target, WM_APP_LOADED, 0, reinterpret_cast<LPARAM>(c.get()))) c.release();
    }
    if (SUCCEEDED(co)) CoUninitialize();
}

void QuickLookWindow::openFile() {
    if (!path_.empty()) {
        AllowSetForegroundWindow(ASFW_ANY);   // l'app ouverte passe devant (le clic nous en donne le droit)
        ShellExecuteW(nullptr, nullptr, path_.c_str(), nullptr, nullptr, SW_SHOWNORMAL);   // verbe par défaut
    }
    open_ = false;
    onClose();
}

RECT QuickLookWindow::targetRect() {
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(ownerUi_ ? ownerUi_ : hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
    // Plein écran sous la barre de menus (toujours au premier plan, elle cacherait les boutons de la barre d'outils).
    if (fullscreen_) return RECT{mi.rcMonitor.left, mi.rcWork.top, mi.rcMonitor.right, mi.rcMonitor.bottom};
    const RECT work = mi.rcWork;
    const SIZE screenPt{LONG((work.right - work.left) / scale_), LONG((work.bottom - work.top) / scale_)};
    SIZE pt{560, 220 + LONG(kToolbar)};
    if (content_->shellPreview) {
        pt = quickLookDocumentSize(path_, screenPt, int(kToolbar));
    } else if (content_->kind == Content::Kind::Image) {
        pt = quickLookWindowSize(SIZE{LONG(content_->size.cx / scale_), LONG(content_->size.cy / scale_)}, screenPt, int(kToolbar));
    } else if (content_->kind == Content::Kind::Text) {
        pt = SIZE{std::min<LONG>(700, LONG(screenPt.cx * 0.9)), std::min<LONG>(560, LONG(screenPt.cy * 0.9) - LONG(kToolbar)) + LONG(kToolbar)};
    }
    const int w = int(std::lround(pt.cx * scale_)), h = int(std::lround(pt.cy * scale_));
    const int x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    return RECT{x, y, x + w, y + h};
}

RECT QuickLookWindow::contentRect() const {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    rc.top = std::min<LONG>(rc.bottom, LONG(std::lround(kToolbar * scale_)));
    return rc;
}

void QuickLookWindow::place() {
    if (!content_) return;
    const RECT to = targetRect();
    if (!visible_ && !fullscreen_) {
        // Ouverture en zoom depuis l'icône du fichier (ou depuis le centre, en plus petit), avec un fondu.
        RECT from = focusedItemRect();
        if (from.right - from.left < 8 || from.bottom - from.top < 8 || from.right - from.left > to.right - to.left) from = {};
        if (from.right <= from.left) {
            const LONG cx = (to.left + to.right) / 2, cy = (to.top + to.bottom) / 2;
            const LONG hw = (to.right - to.left) * 45 / 100, hh = (to.bottom - to.top) * 45 / 100;
            from = RECT{cx - hw, cy - hh, cx + hw, cy + hh};
        }
        animFrom_ = from;
        animTo_ = to;
        animStart_ = GetTickCount64();
        animating_ = true;
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd_, 0, 0, LWA_ALPHA);
        visible_ = true;
        SetWindowPos(hwnd_, HWND_TOPMOST, from.left, from.top, from.right - from.left, from.bottom - from.top,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        if (rt_) rt_->Resize(D2D1::SizeU(UINT32(from.right - from.left), UINT32(from.bottom - from.top)));
        SetTimer(hwnd_, kAnimTimer, 15, nullptr);
        render();
        return;
    }
    visible_ = true;
    SetWindowPos(hwnd_, HWND_TOPMOST, to.left, to.top, to.right - to.left, to.bottom - to.top, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (rt_) rt_->Resize(D2D1::SizeU(UINT32(to.right - to.left), UINT32(to.bottom - to.top)));
    if (preview_.active()) preview_.resize(contentRect());
    if (media_.active()) media_.resize(contentRect());
    render();
    if (!animating_ && !preview_.active() && !media_.active()) startHosts();
}

void QuickLookWindow::stepAnimation() {
    const double t = (GetTickCount64() - animStart_) / 1000.0 / kAnimSeconds;
    const RECT r = quickLookZoom(animFrom_, animTo_, t);
    SetWindowPos(hwnd_, HWND_TOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOACTIVATE);
    if (rt_) rt_->Resize(D2D1::SizeU(UINT32(r.right - r.left), UINT32(r.bottom - r.top)));
    SetLayeredWindowAttributes(hwnd_, 0, BYTE(std::lround(255 * std::min(1.0, t * 1.6))), LWA_ALPHA);
    render();
    if (t < 1) return;
    KillTimer(hwnd_, kAnimTimer);
    animating_ = false;
    // Fenêtre ordinaire de nouveau : les aperçus (fenêtres enfants, parfois d'un autre processus) s'y affichent mieux.
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, GetWindowLongPtrW(hwnd_, GWL_EXSTYLE) & ~LONG_PTR(WS_EX_LAYERED));
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    startHosts();
}

void QuickLookWindow::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    place();
}

void QuickLookWindow::startHosts() {
    if (!content_ || inHostCall_ || !open_) return;
    const std::uint64_t gen = generation_.load();
    inHostCall_ = true;   // DoPreview et MFPlay peuvent laisser passer nos messages : ils attendent la fin
    bool shown = false;
    if (content_->media != QuickLookMedia::None)
        shown = media_.open(instance_, hwnd_, contentRect(), path_, content_->media == QuickLookMedia::Video &&
                                                                      content_->kind == Content::Kind::Image);
    else if (content_->shellPreview)
        shown = preview_.open(instance_, hwnd_, contentRect(), path_, content_->previewClsid, dark_);
    inHostCall_ = false;
    if (diagnosticCapture() && (content_->media != QuickLookMedia::None || content_->shellPreview))
        log::info(L"[diag] coup d'œil : aperçu %s", shown ? L"affiché" : L"impossible (miniature gardée)");
    if (shown && (gen != generation_.load() || !open_)) stopHosts();   // fermé ou remplacé pendant l'ouverture
    render();
    std::vector<MSG> later;
    later.swap(deferred_);
    for (const MSG& m : later) handle(m.message, m.wParam, m.lParam);
}

void QuickLookWindow::stopHosts() {
    preview_.close();
    media_.close();
}

int QuickLookWindow::hitButton(POINT p) const {
    if (PtInRect(&closeRc_, p)) return 1;
    if (PtInRect(&openRc_, p)) return 2;
    if (PtInRect(&fullRc_, p)) return 3;
    return 0;
}

void QuickLookWindow::render() {
    if (!hwnd_ || !content_) return;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    if (!rt_) {
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
                                                        96, 96);
        if (FAILED(d2d_->CreateHwndRenderTarget(props, D2D1::HwndRenderTargetProperties(hwnd_, D2D1::SizeU(UINT32(rc.right), UINT32(rc.bottom))),
                                                &rt_)))
            return;
    }
    const float s = scale_, W = float(rc.right), H = float(rc.bottom), bar = kToolbar * s;
    const Palette pal = paletteFor(dark_ || fullscreen_);
    // Boutons : fermer et plein écran à gauche, « Ouvrir avec… » à droite (largeur selon son texte).
    closeRc_ = RECT{LONG(kPad * s), LONG((kToolbar - kClose) / 2 * s), LONG((kPad + kClose) * s), LONG((kToolbar + kClose) / 2 * s)};
    fullRc_ = RECT{closeRc_.right + LONG(6 * s), closeRc_.top, closeRc_.right + LONG((6 + kClose) * s), closeRc_.bottom};
    const std::wstring ui = firstFont(dwrite_.Get(), {L"SF Pro Text"});
    {
        float textW = 120;
        ComPtr<IDWriteTextFormat> f;
        ComPtr<IDWriteTextLayout> l;
        if (SUCCEEDED(dwrite_->CreateTextFormat(ui.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                                                DWRITE_FONT_STRETCH_NORMAL, 13 * s, L"fr-FR", &f)) &&
            SUCCEEDED(dwrite_->CreateTextLayout(content_->openWith.c_str(), UINT32(content_->openWith.size()), f.Get(), 2000, 100, &l))) {
            DWRITE_TEXT_METRICS m{};
            l->GetMetrics(&m);
            textW = m.width / s;
        }
        const float bw = textW + 24;
        openRc_ = RECT{LONG(W - (kPad + bw) * s), LONG((kToolbar - kButtonH) / 2 * s), LONG(W - kPad * s), LONG((kToolbar + kButtonH) / 2 * s)};
    }
    if (!bitmap_ && !content_->pixels.empty()) {
        const auto bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        rt_->CreateBitmap(D2D1::SizeU(UINT32(content_->size.cx), UINT32(content_->size.cy)), content_->pixels.data(),
                          UINT32(content_->size.cx * 4), bp, &bitmap_);
    }
    auto brush = [&](D2D1_COLOR_F c) {
        ComPtr<ID2D1SolidColorBrush> b;
        rt_->CreateSolidColorBrush(c, &b);
        return b;
    };
    auto format = [&](const std::wstring& family, float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
        ComPtr<IDWriteTextFormat> f;
        dwrite_->CreateTextFormat(family.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size * s,
                                  L"fr-FR", &f);
        if (f) {
            f->SetTextAlignment(align);
            f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
            DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            f->SetTrimming(&trim, nullptr);
        }
        return f;
    };

    rt_->BeginDraw();
    rt_->Clear(fullscreen_ ? D2D1::ColorF(0, 0, 0) : content_->kind == Content::Kind::Text ? pal.paper : pal.background);
    rt_->FillRectangle(D2D1::RectF(0, 0, W, bar), brush(pal.toolbar).Get());
    rt_->FillRectangle(D2D1::RectF(0, bar - std::max(1.f, 0.5f * s), W, bar), brush(pal.separator).Get());
    auto ink = brush(pal.ink);
    {   // ×
        const float cx = (closeRc_.left + closeRc_.right) / 2.f, cy = (closeRc_.top + closeRc_.bottom) / 2.f, r = kClose / 2 * s;
        if (hover_ == 1) rt_->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), brush(pal.buttonHot).Get());
        const float g = 4.5f * s;
        rt_->DrawLine({cx - g, cy - g}, {cx + g, cy + g}, ink.Get(), 1.6f * s);
        rt_->DrawLine({cx + g, cy - g}, {cx - g, cy + g}, ink.Get(), 1.6f * s);
    }
    {   // plein écran : deux flèches en diagonale (vers l'extérieur, ou vers l'intérieur pour revenir)
        const float cx = (fullRc_.left + fullRc_.right) / 2.f, cy = (fullRc_.top + fullRc_.bottom) / 2.f, r = kClose / 2 * s;
        if (hover_ == 3) rt_->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), brush(pal.buttonHot).Get());
        const float g = 5.0f * s, h = 3.0f * s, w = 1.5f * s;
        const float sign = fullscreen_ ? -1.f : 1.f;
        for (int k : {-1, 1}) {   // coin haut droit, puis bas gauche
            const float tipX = cx + k * g * sign, tipY = cy - k * g * sign;
            const float baseX = cx + k * 1.0f * s * sign, baseY = cy - k * 1.0f * s * sign;
            rt_->DrawLine({baseX, baseY}, {tipX, tipY}, ink.Get(), w);
            rt_->DrawLine({tipX, tipY}, {tipX - k * h * sign, tipY}, ink.Get(), w);
            rt_->DrawLine({tipX, tipY}, {tipX, tipY + k * h * sign}, ink.Get(), w);
        }
    }
    if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER))
        rt_->DrawTextW(content_->name.c_str(), UINT32(content_->name.size()), f.Get(),
                       D2D1::RectF(float(fullRc_.right) + 12 * s, 0, float(openRc_.left) - 12 * s, bar), ink.Get());
    {
        const D2D1_ROUNDED_RECT b{D2D1::RectF(float(openRc_.left), float(openRc_.top), float(openRc_.right), float(openRc_.bottom)),
                                  6 * s, 6 * s};
        rt_->FillRoundedRectangle(b, brush(hover_ == 2 ? pal.buttonHot : pal.button).Get());
        rt_->DrawRoundedRectangle(b, brush(pal.separator).Get(), std::max(1.f, 0.5f * s));
        if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER))
            rt_->DrawTextW(content_->openWith.c_str(), UINT32(content_->openWith.size()), f.Get(), b.rect, ink.Get());
    }
    const D2D1_RECT_F area = D2D1::RectF(0, bar, W, H);
    // Un aperçu du Shell ou une vidéo couvre la zone (fenêtre enfant) : rien à dessiner dessous.
    const bool covered = preview_.active() || (media_.active() && content_->media == QuickLookMedia::Video &&
                                                content_->kind == Content::Kind::Image);
    switch (covered ? Content::Kind(-1) : content_->kind) {
        case Content::Kind::Image:
            if (bitmap_) {   // ajustée à la zone, proportions gardées
                const float iw = float(content_->size.cx), ih = float(content_->size.cy);
                const float k = std::min((area.right - area.left) / iw, (area.bottom - area.top) / ih);
                const float dw = iw * k, dh = ih * k;
                const float x = (area.left + area.right - dw) / 2, y = (area.top + area.bottom - dh) / 2;
                rt_->DrawBitmap(bitmap_.Get(), D2D1::RectF(x, y, x + dw, y + dh), 1, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            }
            break;
        case Content::Kind::Text: {
            ComPtr<IDWriteTextFormat> f;
            dwrite_->CreateTextFormat(firstFont(dwrite_.Get(), {L"SF Mono", L"Cascadia Mono", L"Consolas"}).c_str(), nullptr,
                                      DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12 * s,
                                      L"fr-FR", &f);
            if (f) {
                f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                const D2D1_RECT_F box = D2D1::RectF(16 * s, bar + 12 * s, W - 16 * s, H - 8 * s);
                rt_->PushAxisAlignedClip(box, D2D1_ANTIALIAS_MODE_ALIASED);
                // Ce qui tient dans la fenêtre seulement : quelques milliers de caractères suffisent.
                const std::size_t n = std::min<std::size_t>(content_->text.size(), 20000);
                rt_->DrawTextW(content_->text.c_str(), UINT32(n), f.Get(), D2D1::RectF(box.left, box.top, box.right, box.bottom + 100000),
                               ink.Get());
                rt_->PopAxisAlignedClip();
            }
            break;
        }
        case Content::Kind::Icon: {   // grande icône à gauche, nom, type, taille et date à droite (comme le Finder)
            const float icon = 128 * s, left = 32 * s, cy = (area.top + area.bottom) / 2;
            if (bitmap_)
                rt_->DrawBitmap(bitmap_.Get(), D2D1::RectF(left, cy - icon / 2, left + icon, cy + icon / 2), 1,
                                D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
            const float tx = left + icon + 24 * s;
            if (auto f = format(ui, 17, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_LEADING))
                rt_->DrawTextW(content_->name.c_str(), UINT32(content_->name.size()), f.Get(),
                               D2D1::RectF(tx, cy - 40 * s, W - 24 * s, cy - 14 * s), ink.Get());
            if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING)) {
                std::wstring kind = content_->kindName;
                if (content_->media == QuickLookMedia::Audio && media_.active())   // son en cours : un clic met en pause
                    kind += media_.paused() ? L" — en pause" : L" — lecture";
                rt_->DrawTextW(kind.c_str(), UINT32(kind.size()), f.Get(), D2D1::RectF(tx, cy - 10 * s, W - 24 * s, cy + 10 * s),
                               brush(pal.secondary).Get());
                rt_->DrawTextW(content_->details.c_str(), UINT32(content_->details.size()), f.Get(),
                               D2D1::RectF(tx, cy + 12 * s, W - 24 * s, cy + 32 * s), brush(pal.secondary).Get());
            }
            break;
        }
        default: break;
    }
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        rt_.Reset();
        bitmap_.Reset();
    }
}

LRESULT CALLBACK QuickLookWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<QuickLookWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && (self->hwnd_ == h || !self->hwnd_)) {
        if (!self->hwnd_) self->hwnd_ = h;   // messages de création
        return self->handle(msg, wp, lp);
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT QuickLookWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    // Pendant DoPreview ou l'ouverture d'un média, la boucle modale de COM peut livrer ces messages : rejoués après.
    if (inHostCall_ && (msg == WM_APP_SHOW || msg == WM_APP_CLOSE || msg == WM_APP_LOADED || msg == WM_LBUTTONUP ||
                        msg == WM_TIMER || msg == WM_CLOSE)) {
        deferred_.push_back(MSG{hwnd_, msg, wp, lp, 0, {}});
        return 0;
    }
    switch (msg) {
        case WM_APP_SHOW: {
            std::unique_ptr<ShowRequest> r(reinterpret_cast<ShowRequest*>(lp));
            if (r && open_) onShow(*r);
            return 0;
        }
        case WM_APP_CLOSE:
            if (!open_) onClose();   // sinon, rouvert depuis : la demande suivante remplace le contenu
            return 0;
        case WM_APP_LOADED: {
            std::unique_ptr<Content> c(reinterpret_cast<Content*>(lp));
            if (diagnosticCapture())
                log::info(L"[diag] coup d'œil : chargé (genre %d, %ldx%ld, aperçu %d, média %d)", c ? int(c->kind) : -1,
                          c ? c->size.cx : 0, c ? c->size.cy : 0, c ? int(c->shellPreview) : 0, c ? int(c->media) : 0);
            if (!c || c->generation != generation_.load() || !open_) return 0;   // fermé ou remplacé depuis
            stopHosts();
            content_ = std::move(c);
            bitmap_.Reset();
            place();
            return 0;
        }
        case WM_TIMER:
            if (wp == kAnimTimer) stepAnimation();
            return 0;
        case WM_CLOSE:   // arrêt du Dock : fin de la boucle du fil
            PostQuitMessage(0);
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'Explorateur garde le focus (flèches, Espace)
        case WM_NCHITTEST: {
            POINT p{short(LOWORD(lp)), short(HIWORD(lp))};
            ScreenToClient(hwnd_, &p);
            if (!fullscreen_ && p.y < LONG(kToolbar * scale_) && !hitButton(p)) return HTCAPTION;   // déplacer par la barre
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, hwnd_, 0};
            TrackMouseEvent(&t);
            const int hot = hitButton(POINT{short(LOWORD(lp)), short(HIWORD(lp))});
            if (hot != hover_) {
                hover_ = hot;
                render();
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (hover_) {
                hover_ = 0;
                render();
            }
            return 0;
        case WM_LBUTTONUP:
            switch (hitButton(POINT{short(LOWORD(lp)), short(HIWORD(lp))})) {
                case 1:
                    open_ = false;
                    onClose();
                    break;
                case 2: openFile(); break;
                case 3: toggleFullscreen(); break;
                default:
                    if (media_.active() && content_ && content_->media == QuickLookMedia::Audio) {   // son : pause ⇄ lecture
                        media_.toggle();
                        render();
                    }
                    break;
            }
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            render();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_DESTROY: {
            for (MSG m; PeekMessageW(&m, hwnd_, WM_APP_LOADED, WM_APP_LOADED, PM_REMOVE);) delete reinterpret_cast<Content*>(m.lParam);
            for (MSG m; PeekMessageW(&m, hwnd_, WM_APP_SHOW, WM_APP_SHOW, PM_REMOVE);) delete reinterpret_cast<ShowRequest*>(m.lParam);
            rt_.Reset();
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace md
