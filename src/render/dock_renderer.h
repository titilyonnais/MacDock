// Rendu du Dock : DirectComposition + Direct2D + DirectWrite.
#pragma once
#include <windows.h>
#include <d2d1_3.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite_3.h>
#include <wrl/client.h>

#include <array>
#include <cstdint>
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

struct OverlayImage {          // image de référence superposée (calibration)
    UINT w = 0, h = 0;
    std::vector<std::uint8_t> bgra;   // BGRA prémultiplié
};

struct RenderFrame {
    float bgLeft = 0, bgTop = 0, bgRight = 0, bgBottom = 0;
    float cornerRadius = 0, scale = 1;
    bool dark = false;
    std::vector<RenderIcon> icons;
    RenderTooltip tooltip;
    DockPosition position = DockPosition::Bottom;
    // Superposition de calibration : calée en bas et au centre, à l'échelle overlayScale, par-dessus tout.
    std::shared_ptr<const OverlayImage> overlay;
    float overlayOpacity = 0.5f, overlayScale = 1;
};

class DockRenderer {
public:
    bool init(HWND hwnd);
    bool initOffscreen();   // device WARP, sans fenêtre ni DirectComposition (tests, captures)
    void resize(UINT w, UINT h);
    // false => périphérique perdu : rappeler init().
    bool render(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily);
    void releaseImages() { bitmaps_.clear(); }
    // Rendu dans une image BGRA prémultipliée w x h, sur un fond donné (BGRA w x h ; vide = dégradé factice).
    std::vector<std::uint8_t> renderToBgra(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily,
                                           UINT w, UINT h, const std::vector<std::uint8_t>& wallpaper = {});
    // Rendu hors écran vers un PNG (diagnostic, calibration).
    bool renderToFile(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily, UINT w, UINT h,
                      const std::wstring& path, const std::vector<std::uint8_t>& wallpaper = {});
    ID3D11Device* device() const { return d3d_.Get(); }

private:
    bool createDevices(bool warpOnly);
    // Rectangle à coins continus (Apple), en pixels ; la dernière géométrie est gardée en cache.
    ID2D1Geometry* smoothRect(D2D1_RECT_F r, float radius);
    ID2D1Bitmap1* bitmapFor(const IconProvider::ImagePtr& img);
    std::wstring resolveFont(const std::wstring& wanted);
    void drawBackground(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m);
    void drawTooltip(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font);
    void drawFrame(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font);
    void drawOverlay(ID2D1DeviceContext* dc, const RenderFrame& f);

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
    struct CachedGeometry { std::array<long, 5> key{}; Com<ID2D1PathGeometry> geometry; };
    std::vector<CachedGeometry> geometries_;   // quelques formes par image (fond, infobulle)
    std::weak_ptr<const OverlayImage> overlayOwner_;
    Com<ID2D1Bitmap1> overlayBitmap_;
    std::wstring fontWanted_, fontResolved_;
};

} // namespace md
