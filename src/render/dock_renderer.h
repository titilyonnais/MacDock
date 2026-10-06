// Rendu du Dock : DirectComposition + Direct2D + DirectWrite.
#pragma once
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../config/metrics.h"
#include "../config/settings.h"
#include "../icons/icon_provider.h"

namespace md {

struct RenderIcon {            // en pixels de la fenêtre (y vers le bas)
    float cx = 0, cy = 0, size = 0;
    IconProvider::ImagePtr image;
    bool indicator = false;
    float indicatorY = 0;      // centre du point indicateur
    bool separator = false;
    float sepLength = 0;
    float opacity = 1;
};

struct RenderTooltip {
    bool visible = false;
    std::wstring text;
    float cx = 0, bottom = 0, opacity = 0;
};

struct RenderFrame {
    float bgLeft = 0, bgTop = 0, bgRight = 0, bgBottom = 0;
    float cornerRadius = 0, scale = 1;
    bool dark = false;
    std::vector<RenderIcon> icons;
    RenderTooltip tooltip;
    DockPosition position = DockPosition::Bottom;
};

class DockRenderer {
public:
    bool init(HWND hwnd);
    void resize(UINT w, UINT h);
    // false => périphérique perdu : rappeler init().
    bool render(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily);
    void releaseImages() { bitmaps_.clear(); }
    // Rendu hors écran vers un PNG (diagnostic, calibration) sur un fond de bureau factice.
    bool renderToFile(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily, UINT w, UINT h,
                      const std::wstring& path);

private:
    ID2D1Bitmap1* bitmapFor(const IconProvider::ImagePtr& img);
    std::wstring resolveFont(const std::wstring& wanted);
    void drawBackground(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m);
    void drawTooltip(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font);
    void drawFrame(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font);

    template <class T> using Com = Microsoft::WRL::ComPtr<T>;
    HWND hwnd_ = nullptr;
    UINT width_ = 0, height_ = 0;
    Com<ID3D11Device> d3d_;
    Com<ID2D1Factory3> d2dFactory_;
    Com<ID2D1Device2> d2dDevice_;
    Com<ID2D1DeviceContext2> dc_;
    Com<IDCompositionDesktopDevice> dcomp_;
    Com<IDCompositionTarget> target_;
    Com<IDCompositionVisual2> visual_;
    Com<IDCompositionSurface> surface_;
    Com<IDWriteFactory3> dwrite_;
    Com<ID2D1Effect> shadow_;
    struct CachedBitmap { std::weak_ptr<const IconProvider::Image> owner; Com<ID2D1Bitmap1> bitmap; };
    std::map<const IconProvider::Image*, CachedBitmap> bitmaps_;
    std::wstring fontWanted_, fontResolved_;
};

} // namespace md
