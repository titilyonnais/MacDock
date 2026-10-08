#include "quicklook_window.h"

#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
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

} // namespace

QuickLookWindow::~QuickLookWindow() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
    }
    wake_.notify_one();
    if (worker_.joinable()) worker_.join();   // avant la fenêtre : le fil y poste ses résultats
    if (hwnd_) DestroyWindow(hwnd_);
}

bool QuickLookWindow::ensureWindow() {
    if (hwnd_) return true;
    WNDCLASSEXW wc{sizeof wc};
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = proc;
    wc.hInstance = instance_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    // Flottante (au-dessus de l'Explorateur actif, qu'une fenêtre sans activation ne pourrait pas passer).
    hwnd_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"Coup d'œil", WS_POPUP, 0, 0, 10, 10, nullptr,
                            nullptr, instance_, this);
    if (!hwnd_) {
        log::warn(L"Coup d'œil : fenêtre impossible (%lu)", GetLastError());
        return false;
    }
    const DWORD round = 2;   // DWMWCP_ROUND
    DwmSetWindowAttribute(hwnd_, 33, &round, sizeof round);
    if (!d2d_) D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf());
    if (!dwrite_)
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()));
    return d2d_ && dwrite_;
}

void QuickLookWindow::show(HINSTANCE instance, std::vector<std::wstring> paths, std::size_t index, HWND owner) {
    if (paths.empty()) return;
    instance_ = instance;
    if (!ensureWindow()) return;
    paths_ = std::move(paths);
    index_ = std::min(index, paths_.size() - 1);
    owner_ = owner;
    dark_ = appsDark();
    UINT dx = 96, dy = 96;
    GetDpiForMonitor(MonitorFromWindow(owner ? owner : hwnd_, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dx, &dy);
    scale_ = dx / 96.f;
    open_ = true;
    if (diagnosticCapture()) log::info(L"[diag] coup d'œil : %s (fenêtre %p)", paths_[index_].c_str(), static_cast<void*>(hwnd_));
    startLoad();
}

void QuickLookWindow::startLoad() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = Request{paths_[index_], ++generation_, scale_, hwnd_};   // remplace une demande pas encore prise
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

void QuickLookWindow::close() {
    ++generation_;   // un chargement en cours sera ignoré
    open_ = false;
    if (hwnd_) ShowWindow(hwnd_, SW_HIDE);
    content_.reset();
    bitmap_.Reset();
    hover_ = 0;
}

void QuickLookWindow::openFile() {
    if (!paths_.empty() && index_ < paths_.size()) {
        AllowSetForegroundWindow(ASFW_ANY);   // l'app ouverte passe devant (le clic nous en donne le droit)
        ShellExecuteW(nullptr, nullptr, paths_[index_].c_str(), nullptr, nullptr, SW_SHOWNORMAL);   // verbe par défaut
    }
    close();
}

void QuickLookWindow::place() {
    if (!content_) return;
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromWindow(owner_ ? owner_ : hwnd_, MONITOR_DEFAULTTONEAREST), &mi);
    const RECT work = mi.rcWork;
    const SIZE screenPt{LONG((work.right - work.left) / scale_), LONG((work.bottom - work.top) / scale_)};
    SIZE contentPt{560, 220};
    if (content_->kind == Content::Kind::Image)
        contentPt = SIZE{LONG(content_->size.cx / scale_), LONG(content_->size.cy / scale_)};
    else if (content_->kind == Content::Kind::Text)
        contentPt = SIZE{700, 560};
    // Jamais plus grand que l'écran (1080p à 200 % : 960 × 540 points), barre d'outils comprise.
    if (content_->kind != Content::Kind::Image) {
        contentPt.cx = std::min<LONG>(contentPt.cx, LONG(screenPt.cx * 0.9));
        contentPt.cy = std::min<LONG>(contentPt.cy, LONG(screenPt.cy * 0.9) - LONG(kToolbar));
    }
    SIZE pt = content_->kind == Content::Kind::Image ? quickLookWindowSize(contentPt, screenPt, int(kToolbar))
                                                     : SIZE{contentPt.cx, contentPt.cy + LONG(kToolbar)};
    const int w = int(std::lround(pt.cx * scale_)), h = int(std::lround(pt.cy * scale_));
    const int x = work.left + (work.right - work.left - w) / 2, y = work.top + (work.bottom - work.top - h) / 2;
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    if (rt_) rt_->Resize(D2D1::SizeU(UINT32(w), UINT32(h)));
    const float s = scale_;
    closeRc_ = RECT{LONG(kPad * s), LONG((kToolbar - kClose) / 2 * s), LONG((kPad + kClose) * s), LONG((kToolbar + kClose) / 2 * s)};
    // Bouton « Ouvrir avec… » : largeur selon son texte.
    float textW = 120;
    ComPtr<IDWriteTextFormat> f;
    ComPtr<IDWriteTextLayout> l;
    if (SUCCEEDED(dwrite_->CreateTextFormat(firstFont(dwrite_.Get(), {L"SF Pro Text"}).c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13 * s, L"fr-FR", &f)) &&
        SUCCEEDED(dwrite_->CreateTextLayout(content_->openWith.c_str(), UINT32(content_->openWith.size()), f.Get(), 2000, 100, &l))) {
        DWRITE_TEXT_METRICS m{};
        l->GetMetrics(&m);
        textW = m.width / s;
    }
    const float bw = textW + 24;
    openRc_ = RECT{LONG(w - (kPad + bw) * s), LONG((kToolbar - kButtonH) / 2 * s), LONG(w - kPad * s),
                   LONG((kToolbar + kButtonH) / 2 * s)};
}

int QuickLookWindow::hitButton(POINT p) const {
    if (PtInRect(&closeRc_, p)) return 1;
    if (PtInRect(&openRc_, p)) return 2;
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
    const Palette pal = paletteFor(dark_);
    if (!bitmap_ && !content_->pixels.empty()) {
        const auto bp = D2D1::BitmapProperties(D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        rt_->CreateBitmap(D2D1::SizeU(UINT32(content_->size.cx), UINT32(content_->size.cy)), content_->pixels.data(),
                          UINT32(content_->size.cx * 4), bp, &bitmap_);
    }
    const std::wstring ui = firstFont(dwrite_.Get(), {L"SF Pro Text"});
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
    rt_->Clear(content_->kind == Content::Kind::Text ? pal.paper : pal.background);
    // Barre d'outils : fermer à gauche, nom centré, « Ouvrir avec… » à droite.
    rt_->FillRectangle(D2D1::RectF(0, 0, W, bar), brush(pal.toolbar).Get());
    rt_->FillRectangle(D2D1::RectF(0, bar - std::max(1.f, 0.5f * s), W, bar), brush(pal.separator).Get());
    {
        const float cx = (closeRc_.left + closeRc_.right) / 2.f, cy = (closeRc_.top + closeRc_.bottom) / 2.f, r = kClose / 2 * s;
        if (hover_ == 1) rt_->FillEllipse(D2D1::Ellipse({cx, cy}, r, r), brush(pal.buttonHot).Get());
        const float g = 4.5f * s;
        auto ink = brush(pal.ink);
        rt_->DrawLine({cx - g, cy - g}, {cx + g, cy + g}, ink.Get(), 1.6f * s);
        rt_->DrawLine({cx + g, cy - g}, {cx - g, cy + g}, ink.Get(), 1.6f * s);
    }
    if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER))
        rt_->DrawTextW(content_->name.c_str(), UINT32(content_->name.size()), f.Get(),
                       D2D1::RectF(float(closeRc_.right) + 12 * s, 0, float(openRc_.left) - 12 * s, bar), brush(pal.ink).Get());
    {
        const D2D1_ROUNDED_RECT b{D2D1::RectF(float(openRc_.left), float(openRc_.top), float(openRc_.right), float(openRc_.bottom)),
                                  6 * s, 6 * s};
        rt_->FillRoundedRectangle(b, brush(hover_ == 2 ? pal.buttonHot : pal.button).Get());
        rt_->DrawRoundedRectangle(b, brush(pal.separator).Get(), std::max(1.f, 0.5f * s));
        if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_CENTER))
            rt_->DrawTextW(content_->openWith.c_str(), UINT32(content_->openWith.size()), f.Get(), b.rect, brush(pal.ink).Get());
    }
    const D2D1_RECT_F area = D2D1::RectF(0, bar, W, H);
    switch (content_->kind) {
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
                               brush(pal.ink).Get());
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
                               D2D1::RectF(tx, cy - 40 * s, W - 24 * s, cy - 14 * s), brush(pal.ink).Get());
            if (auto f = format(ui, 13, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_TEXT_ALIGNMENT_LEADING)) {
                rt_->DrawTextW(content_->kindName.c_str(), UINT32(content_->kindName.size()), f.Get(),
                               D2D1::RectF(tx, cy - 10 * s, W - 24 * s, cy + 10 * s), brush(pal.secondary).Get());
                rt_->DrawTextW(content_->details.c_str(), UINT32(content_->details.size()), f.Get(),
                               D2D1::RectF(tx, cy + 12 * s, W - 24 * s, cy + 32 * s), brush(pal.secondary).Get());
            }
            break;
        }
    }
    if (rt_->EndDraw() == D2DERR_RECREATE_TARGET) {
        rt_.Reset();
        bitmap_.Reset();
    }
}

LRESULT CALLBACK QuickLookWindow::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<QuickLookWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && self->hwnd_ == h) return self->handle(msg, wp, lp);
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT QuickLookWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_APP_LOADED: {
            std::unique_ptr<Content> c(reinterpret_cast<Content*>(lp));
            if (diagnosticCapture())
                log::info(L"[diag] coup d'œil : chargé (genre %d, %ldx%ld)", c ? int(c->kind) : -1, c ? c->size.cx : 0,
                          c ? c->size.cy : 0);
            if (!c || c->generation != generation_.load() || !open_) return 0;   // fermé ou remplacé depuis
            content_ = std::move(c);
            bitmap_.Reset();
            place();
            render();
            return 0;
        }
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'Explorateur garde le focus (flèches, Espace)
        case WM_NCHITTEST: {
            POINT p{short(LOWORD(lp)), short(HIWORD(lp))};
            ScreenToClient(hwnd_, &p);
            if (p.y < LONG(kToolbar * scale_) && !hitButton(p)) return HTCAPTION;   // déplacer par la barre
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
                case 1: close(); break;
                case 2: openFile(); break;
                default: break;
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
            for (MSG m; PeekMessageW(&m, hwnd_, WM_APP_LOADED, WM_APP_LOADED, PM_REMOVE);)
                delete reinterpret_cast<Content*>(m.lParam);
            rt_.Reset();
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

} // namespace md
