#include "screen_recorder.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "../core/log.h"
#include "screenshot_logic.h"

using Microsoft::WRL::ComPtr;

namespace md {

struct ScreenRecorder::Impl {
    std::thread thread;
    HANDLE stop = nullptr;           // signalé par stop()
    std::atomic<LONGLONG> elapsed{0};   // 100 ns
    std::mutex lastLock;
    std::vector<std::uint8_t> last;  // dernière image (vignette)
    int w = 0, h = 0;
    bool finalized = false;
    ~Impl() {
        if (stop) CloseHandle(stop);
    }
};

namespace {

// Copie de la zone de l'écran à la taille de la vidéo, curseur compris (GDI : les fenêtres exclues n'y sont pas).
struct ScreenSource {
    RECT area{};
    int w = 0, h = 0;
    HDC screen = nullptr, mem = nullptr;
    HBITMAP dib = nullptr;
    HGDIOBJ old = nullptr;
    void* bits = nullptr;
    bool init(const RECT& a, int vw, int vh) {
        area = a;
        w = vw;
        h = vh;
        screen = GetDC(nullptr);
        mem = CreateCompatibleDC(screen);
        BITMAPINFO bi{};
        bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
        dib = CreateDIBSection(mem, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!dib) return false;
        old = SelectObject(mem, dib);
        SetStretchBltMode(mem, HALFTONE);
        SetBrushOrgEx(mem, 0, 0, nullptr);
        return true;
    }
    void grab(std::uint8_t* out) {
        const int aw = area.right - area.left, ah = area.bottom - area.top;
        StretchBlt(mem, 0, 0, w, h, screen, area.left, area.top, aw, ah, SRCCOPY | CAPTUREBLT);
        CURSORINFO ci{sizeof ci};
        if (GetCursorInfo(&ci) && (ci.flags & CURSOR_SHOWING) && PtInRect(&area, ci.ptScreenPos)) {
            ICONINFO ii{};
            if (GetIconInfo(ci.hCursor, &ii)) {
                const double kx = double(w) / aw, ky = double(h) / ah;
                const int cx = int((ci.ptScreenPos.x - area.left - LONG(ii.xHotspot)) * kx);
                const int cy = int((ci.ptScreenPos.y - area.top - LONG(ii.yHotspot)) * ky);
                const int size = std::max(8, int(GetSystemMetrics(SM_CXCURSOR) * kx));
                DrawIconEx(mem, cx, cy, ci.hCursor, size, size, 0, nullptr, DI_NORMAL);
                if (ii.hbmMask) DeleteObject(ii.hbmMask);
                if (ii.hbmColor) DeleteObject(ii.hbmColor);
            }
        }
        GdiFlush();
        std::memcpy(out, bits, std::size_t(w) * h * 4);
    }
    ~ScreenSource() {
        if (old) SelectObject(mem, old);
        if (dib) DeleteObject(dib);
        if (mem) DeleteDC(mem);
        if (screen) ReleaseDC(nullptr, screen);
    }
};

ComPtr<IMFSinkWriter> openWriter(const std::wstring& path, int w, int h, int fps, DWORD& stream) {
    ComPtr<IMFAttributes> attrs;
    ComPtr<IMFSinkWriter> writer;
    MFCreateAttributes(&attrs, 1);
    if (attrs) attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    if (FAILED(MFCreateSinkWriterFromURL(path.c_str(), nullptr, attrs.Get(), &writer))) return nullptr;
    ComPtr<IMFMediaType> out, in;
    if (FAILED(MFCreateMediaType(&out)) || FAILED(MFCreateMediaType(&in))) return nullptr;
    out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    out->SetUINT32(MF_MT_AVG_BITRATE, UINT32(std::clamp(double(w) * h * fps * 0.12, 1.0e6, 2.0e7)));
    out->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    MFSetAttributeSize(out.Get(), MF_MT_FRAME_SIZE, UINT32(w), UINT32(h));
    MFSetAttributeRatio(out.Get(), MF_MT_FRAME_RATE, UINT32(fps), 1);
    MFSetAttributeRatio(out.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer->AddStream(out.Get(), &stream))) return nullptr;
    in->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    in->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    in->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    in->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(w * 4));   // positif : image de haut en bas
    MFSetAttributeSize(in.Get(), MF_MT_FRAME_SIZE, UINT32(w), UINT32(h));
    MFSetAttributeRatio(in.Get(), MF_MT_FRAME_RATE, UINT32(fps), 1);
    MFSetAttributeRatio(in.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    if (FAILED(writer->SetInputMediaType(stream, in.Get(), nullptr)) || FAILED(writer->BeginWriting())) return nullptr;
    return writer;
}

} // namespace

ScreenRecorder::~ScreenRecorder() { stop(); }

bool ScreenRecorder::start(const RECT& area, const std::wstring& path, int fps, FrameSource source) {
    if (running_) return false;
    const SIZE size = recordingSize(SIZE{area.right - area.left, area.bottom - area.top}, 1920);
    auto impl = std::make_shared<Impl>();
    impl->stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl->w = size.cx;
    impl->h = size.cy;
    fps = std::clamp(fps, 1, 60);
    std::promise<bool> ready;
    std::future<bool> started = ready.get_future();
    impl->thread = std::thread([impl, area, path, fps, source, &ready]() mutable {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const HRESULT mf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        DWORD stream = 0;
        ComPtr<IMFSinkWriter> writer = SUCCEEDED(mf) ? openWriter(path, impl->w, impl->h, fps, stream) : nullptr;
        ScreenSource screen;
        const bool ok = writer && (source || screen.init(area, impl->w, impl->h));
        ready.set_value(ok);   // la promesse n'existe plus après
        if (ok) {
            const std::size_t bytes = std::size_t(impl->w) * impl->h * 4;
            std::vector<std::uint8_t> frame(bytes);
            LARGE_INTEGER freq, t0, now;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t0);
            const LONGLONG duration = 10'000'000LL / fps;
            for (LONGLONG n = 0;; ++n) {
                QueryPerformanceCounter(&now);
                const LONGLONG time = (now.QuadPart - t0.QuadPart) * 10'000'000LL / freq.QuadPart;
                if (source) source(frame.data(), impl->w, impl->h);
                else screen.grab(frame.data());
                ComPtr<IMFMediaBuffer> buffer;
                ComPtr<IMFSample> sample;
                BYTE* dst = nullptr;
                if (SUCCEEDED(MFCreateMemoryBuffer(DWORD(bytes), &buffer)) && SUCCEEDED(buffer->Lock(&dst, nullptr, nullptr))) {
                    std::memcpy(dst, frame.data(), bytes);
                    buffer->Unlock();
                    buffer->SetCurrentLength(DWORD(bytes));
                    if (SUCCEEDED(MFCreateSample(&sample)) && SUCCEEDED(sample->AddBuffer(buffer.Get()))) {
                        sample->SetSampleTime(time);
                        sample->SetSampleDuration(duration);
                        writer->WriteSample(stream, sample.Get());
                    }
                }
                impl->elapsed = time + duration;
                {
                    std::lock_guard<std::mutex> lock(impl->lastLock);
                    impl->last = frame;
                }
                // Prochaine image à l'heure (n + 1) / fps ; l'arrêt réveille tout de suite.
                QueryPerformanceCounter(&now);
                const LONGLONG due = (n + 1) * freq.QuadPart / fps - (now.QuadPart - t0.QuadPart);
                const DWORD wait = due > 0 ? DWORD(due * 1000 / freq.QuadPart) : 0;
                if (WaitForSingleObject(impl->stop, wait) == WAIT_OBJECT_0) break;
            }
            impl->finalized = SUCCEEDED(writer->Finalize());
        }
        writer.Reset();
        if (SUCCEEDED(mf)) MFShutdown();
        if (SUCCEEDED(co)) CoUninitialize();
    });
    if (!started.get()) {
        impl->thread.join();
        DeleteFileW(path.c_str());   // jamais de fichier vide sur le Bureau
        log::warn(L"Enregistrement de l'écran : encodeur indisponible (%s)", path.c_str());
        return false;
    }
    impl_ = impl;
    running_ = true;
    return true;
}

bool ScreenRecorder::stop(BgraImage* last) {
    if (!impl_) return false;
    std::shared_ptr<Impl> impl = std::move(impl_);
    running_ = false;
    SetEvent(impl->stop);
    if (impl->thread.joinable()) impl->thread.join();
    if (last) {
        std::lock_guard<std::mutex> lock(impl->lastLock);
        last->w = impl->w;
        last->h = impl->h;
        last->px = impl->last;
        for (std::size_t i = 3; i < last->px.size(); i += 4) last->px[i] = 255;   // GDI laisse l'alpha à 0
    }
    return impl->finalized;
}

double ScreenRecorder::seconds() const {
    const std::shared_ptr<Impl> impl = impl_;
    return impl ? double(impl->elapsed.load()) / 1e7 : 0;
}

} // namespace md
