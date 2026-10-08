#include "quicklook_hosts.h"

#include <propsys.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "../core/log.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {
constexpr wchar_t kVideoClass[] = L"MacDockQuickLookVideo";
constexpr wchar_t kBoxClass[] = L"MacDockQuickLookPreview";

LRESULT CALLBACK boxProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_NCCREATE: SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams)); break;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'Explorateur garde le clavier
        case WM_ERASEBKGND: {   // fond clair ou sombre de cet aperçu (la classe n'est inscrite qu'une fois)
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(reinterpret_cast<HDC>(wp), &rc, static_cast<HBRUSH>(GetStockObject(GetWindowLongPtrW(h, GWLP_USERDATA) ? BLACK_BRUSH : WHITE_BRUSH)));
            return 1;
        }
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// Erreur de lecture (codec absent…) : la fenêtre du Coup d'œil ferme le lecteur et garde la miniature.
class MediaEvents : public IMFPMediaPlayerCallback {
public:
    MediaEvents(HWND notify, UINT msg) : notify_(notify), msg_(msg) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFPMediaPlayerCallback)) {
            *out = static_cast<IMFPMediaPlayerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ULONG(InterlockedIncrement(&ref_)); }
    STDMETHODIMP_(ULONG) Release() override {
        const LONG r = InterlockedDecrement(&ref_);
        if (!r) delete this;
        return ULONG(r);
    }
    void STDMETHODCALLTYPE OnMediaPlayerEvent(MFP_EVENT_HEADER* e) override {
        if (e && (e->eEventType == MFP_EVENT_TYPE_ERROR || FAILED(e->hrEvent))) PostMessageW(notify_, msg_, 0, 0);
    }

private:
    virtual ~MediaEvents() = default;
    LONG ref_ = 1;
    HWND notify_;
    UINT msg_;
};
constexpr wchar_t kPreviewHandlerIid[] = L"{8895b1c6-b41f-4c1c-a562-0d564250836f}";
} // namespace

bool previewHandlerFor(const std::wstring& path, CLSID& clsid) {
    const auto dot = path.find_last_of(L'.');
    const auto slash = path.find_last_of(L"\\/");
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash)) return false;
    wchar_t text[64] = {};
    DWORD n = 64;
    return SUCCEEDED(AssocQueryStringW(ASSOCF_INIT_DEFAULTTOSTAR, ASSOCSTR_SHELLEXTENSION, path.substr(dot).c_str(),
                                       kPreviewHandlerIid, text, &n)) &&
           SUCCEEDED(CLSIDFromString(text, &clsid));
}

// ---- Gestionnaire d'aperçu du Shell ----

bool PreviewHost::open(HINSTANCE instance, HWND parent, const RECT& rc, const std::wstring& path, const CLSID& clsid, bool dark) {
    close();
    ComPtr<IPreviewHandler> handler;
    // Hors processus seulement (prevhost.exe), comme l'Explorateur.
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&handler)))) return false;
    bool ready = false;
    ComPtr<IInitializeWithStream> withStream;
    ComPtr<IInitializeWithItem> withItem;
    ComPtr<IInitializeWithFile> withFile;
    if (SUCCEEDED(handler.As(&withStream))) {   // le plus sûr, recommandé par Microsoft
        ComPtr<IStream> stream;
        ready = SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, 0, FALSE, nullptr, &stream)) &&
                SUCCEEDED(withStream->Initialize(stream.Get(), STGM_READ));
    }
    if (!ready && SUCCEEDED(handler.As(&withItem))) {
        ComPtr<IShellItem> item;
        ready = SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&item))) &&
                SUCCEEDED(withItem->Initialize(item.Get(), STGM_READ));
    }
    if (!ready && SUCCEEDED(handler.As(&withFile))) ready = SUCCEEDED(withFile->Initialize(path.c_str(), STGM_READ));
    if (!ready) return false;
    ComPtr<IPreviewHandlerVisuals> visuals;
    if (SUCCEEDED(handler.As(&visuals))) {
        visuals->SetBackgroundColor(dark ? RGB(30, 30, 30) : RGB(255, 255, 255));
        visuals->SetTextColor(dark ? RGB(245, 245, 247) : RGB(29, 29, 31));
    }
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = boxProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kBoxClass;
    RegisterClassExW(&wc);
    box_ = CreateWindowExW(0, kBoxClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, rc.left, rc.top,
                           rc.right - rc.left, rc.bottom - rc.top, parent, nullptr, instance, reinterpret_cast<void*>(INT_PTR(dark)));
    const RECT inner{0, 0, rc.right - rc.left, rc.bottom - rc.top};
    if (!box_ || FAILED(handler->SetWindow(box_, &inner)) || FAILED(handler->DoPreview())) {
        handler->Unload();
        if (box_) DestroyWindow(box_);
        box_ = nullptr;
        return false;
    }
    handler_ = handler;
    return true;
}

void PreviewHost::resize(const RECT& rc) {
    if (!handler_ || !box_) return;
    SetWindowPos(box_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    const RECT inner{0, 0, rc.right - rc.left, rc.bottom - rc.top};
    handler_->SetRect(&inner);
}

void PreviewHost::close() {
    // Sortis des membres avant l'appel : une réentrance pendant Unload ne peut plus les toucher.
    ComPtr<IPreviewHandler> handler = std::move(handler_);
    const HWND box = box_;
    box_ = nullptr;
    if (handler) handler->Unload();
    if (box) DestroyWindow(box);
}

// ---- Vidéo et son ----

bool MediaHost::open(HINSTANCE instance, HWND parent, const RECT& rc, const std::wstring& path, bool video, HWND notify,
                     UINT errorMsg) {
    close();
    if (video) {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = proc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kVideoClass;
        RegisterClassExW(&wc);
        video_ = CreateWindowExW(0, kVideoClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, rc.left, rc.top, rc.right - rc.left,
                                 rc.bottom - rc.top, parent, nullptr, instance, this);
        if (!video_) return false;
    }
    events_.Attach(new MediaEvents(notify, errorMsg));
    if (FAILED(MFPCreateMediaPlayer(path.c_str(), TRUE, 0, events_.Get(), video_, &player_))) {
        log::warn(L"Coup d'œil : lecture impossible (%s)", path.c_str());
        close();
        return false;
    }
    return true;
}

void MediaHost::resize(const RECT& rc) {
    if (video_) SetWindowPos(video_, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void MediaHost::toggle() {
    if (!player_) return;
    paused_ = !paused_;
    if (paused_) player_->Pause();
    else player_->Play();
}

void MediaHost::close() {
    paused_ = false;
    ComPtr<IMFPMediaPlayer> player = std::move(player_);   // sorti avant l'appel (réentrance)
    const HWND video = video_;
    video_ = nullptr;
    if (player) {
        player->Stop();
        player->Shutdown();
    }
    events_.Reset();
    if (video) DestroyWindow(video);
}

LRESULT CALLBACK MediaHost::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) SetWindowLongPtrW(h, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    auto* self = reinterpret_cast<MediaHost*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);   // avant UpdateVideo, comme le demande MFPlay
            if (self && self->player_) self->player_->UpdateVideo();
            EndPaint(h, &ps);
            return 0;
        }
        case WM_SIZE:
            if (self && self->player_) self->player_->UpdateVideo();
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;   // l'Explorateur garde le clavier
        case WM_LBUTTONUP:
            if (self) self->toggle();
            return 0;
        case WM_NCDESTROY: SetWindowLongPtrW(h, GWLP_USERDATA, 0); break;
        default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

} // namespace md
