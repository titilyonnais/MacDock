// Rendu du Dock : DirectComposition + Direct2D + DirectWrite, verre Liquid Glass (D3D11) sous les icônes.
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
#include "../glass/glass_renderer.h"
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
    float dim = 0;             // assombrissement (icône pressée, cible d'un dépôt) : part de noir
    std::uint64_t window = 0;  // fenêtre réduite : source de la miniature DWM
    bool thumbnail = false;    // la miniature DWM couvre la case : seule la petite icône d'app est dessinée
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
    bool glass = false;   // verre Liquid Glass sur l'arrière-plan capturé ; false = verre dépoli Direct2D
    std::vector<RenderIcon> icons;
    RenderTooltip tooltip;
    DockPosition position = DockPosition::Bottom;
    // Superposition de calibration : calée en bas et au centre, à l'échelle overlayScale, par-dessus tout.
    std::shared_ptr<const OverlayImage> overlay;
    float overlayOpacity = 0.5f, overlayScale = 1;
};

class DockRenderer {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

public:
    // adapter : carte qui pilote l'écran du Dock (texture partagée de la capture) ; nullptr = carte par défaut.
    bool init(HWND hwnd, IDXGIAdapter1* adapter = nullptr);
    bool initOffscreen();   // device WARP, sans fenêtre ni DirectComposition (tests, captures)
    void resize(UINT w, UINT h);
    // false => périphérique perdu : rappeler init().
    bool render(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily);
    void releaseImages() { bitmaps_.clear(); }
    // Rendu dans une image BGRA prémultipliée w x h, sur un fond donné (BGRA w x h ; vide = dégradé factice).
    // Le fond sert aussi d'arrière-plan au verre : le verre est toujours rendu (frame.glass est ignoré).
    std::vector<std::uint8_t> renderToBgra(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily,
                                           UINT w, UINT h, const std::vector<std::uint8_t>& wallpaper = {});
    // Tests : arrière-plan scRGB (RGBA16F) uniforme de valeur value, blanc SDR à sdrWhiteScale.
    std::vector<std::uint8_t> renderToBgraScRgb(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily,
                                                UINT w, UINT h, float value, float sdrWhiteScale);
    // Rendu hors écran vers un PNG (diagnostic, calibration).
    bool renderToFile(const RenderFrame& frame, const Metrics& m, const std::wstring& fontFamily, UINT w, UINT h,
                      const std::wstring& path, const std::vector<std::uint8_t>& wallpaper = {});
    ID3D11Device* device() const { return d3d_.Get(); }
    // Texture de l'arrière-plan du verre (w x h, BGRA8, ou RGBA16F si scRgb), recréée si besoin ; nullptr si impossible.
    ID3D11Texture2D* backdropTexture(UINT w, UINT h, bool scRgb);
    void setBackdropWhite(float sdrWhiteScale) { sdrWhite_ = sdrWhiteScale > 0 ? sdrWhiteScale : 1; }
    bool glassAvailable() const { return glassReady_; }
    bool isWarp() const { return warp_; }
    std::wstring fontName(const std::wstring& wanted) { return resolveFont(wanted); }   // police réellement utilisée
    LUID adapterLuid() const;   // carte du device ({0, 0} si inconnue)
    // Temps GPU moyen (ms) de la passe de verre depuis le dernier appel ; -1 si aucune mesure (mode trace).
    double takeGlassGpuMs();
    void setGpuTiming(bool on) { gpuTiming_ = on; }

private:
    bool createDevices(bool warpOnly, IDXGIAdapter1* adapter = nullptr);
    void beginGpuTimer(ID3D11DeviceContext* ctx);
    void endGpuTimer(ID3D11DeviceContext* ctx);
    // Rectangle à coins continus (Apple), en pixels ; les dernières géométries sont gardées en cache.
    ID2D1Geometry* smoothRect(D2D1_RECT_F r, float radius);
    ID2D1Bitmap1* bitmapFor(const IconProvider::ImagePtr& img);
    std::wstring resolveFont(const std::wstring& wanted);
    // Position de l'infobulle et mise en page du texte ; false si rien à afficher.
    bool tooltipLayout(const RenderFrame& f, const Metrics& m, const std::wstring& font, D2D1_RECT_F& rect,
                       Com<IDWriteTextLayout>& layout);
    // Rendu hors écran commun : fond (wallpaper, blanc si scRgbValue ≥ 0, sinon dégradé) → verre → Dock → lecture.
    std::vector<std::uint8_t> renderOffscreen(const RenderFrame& f, const Metrics& m, const std::wstring& font, UINT w,
                                              UINT h, const std::vector<std::uint8_t>& wallpaper, float scRgbValue,
                                              float sdrWhite);
    // Passe de verre (D3D11) dans glassTex_ ; false si indisponible (repli dépoli).
    bool runGlass(const RenderFrame& f, const Metrics& m, const std::wstring& font, UINT w, UINT h);
    void drawBackground(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m);
    void drawTooltip(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font, bool glass);
    void drawFrame(ID2D1DeviceContext* dc, const RenderFrame& f, const Metrics& m, const std::wstring& font, bool glass);
    void drawOverlay(ID2D1DeviceContext* dc, const RenderFrame& f);

    HWND hwnd_ = nullptr;
    UINT width_ = 0, height_ = 0;
    Com<ID3D11Device> d3d_;
    bool warp_ = false;
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

    // Verre.
    GlassRenderer glass_;
    bool glassReady_ = false;
    Com<ID3D11Texture2D> backdropTex_;
    Com<ID3D11ShaderResourceView> backdropSrv_;
    bool backdropScRgb_ = false;
    float sdrWhite_ = 1;
    Com<ID3D11Texture2D> glassTex_;
    Com<ID3D11RenderTargetView> glassRtv_;
    Com<ID2D1Bitmap1> glassBitmap_;

    // Mesure GPU de la passe de verre (requêtes timestamp, lues sans attente à l'image suivante).
    bool gpuTiming_ = false;
    Com<ID3D11Query> tsDisjoint_, tsBegin_, tsEnd_;
    bool tsPending_ = false;
    double gpuMsSum_ = 0;
    int gpuMsCount_ = 0;
};

} // namespace md
