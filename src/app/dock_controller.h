// État de l'interface du Dock : magnification, rebonds, infobulle, construction des images.
#pragma once
#include <windows.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../anim/spring.h"
#include "../config/metrics.h"
#include "../config/settings.h"
#include "../icons/icon_provider.h"
#include "../layout/dock_geometry.h"
#include "../layout/dock_layout.h"
#include "../model/app_model.h"
#include "../render/dock_renderer.h"

namespace md {

class DockController {
public:
    void init(const Settings& s, const Metrics& m, AppModel* model);
    void setSettings(const Settings& s);
    void setMetrics(const Metrics& m);
    void setViewport(UINT width, UINT height, float scale);

    void setCursor(std::optional<POINT> clientPx);   // nullopt = souris hors du Dock
    bool tick(double dt);                            // true tant qu'une animation est en cours
    RenderFrame buildFrame(bool dark, IconProvider& icons);
    bool consumeDirty();                             // un nouveau rendu est nécessaire

    std::optional<std::size_t> hitTest(POINT clientPx) const;
    const DockItem* itemAt(std::size_t index) const;
    bool isInsideInteractiveZone(POINT clientPx) const;

    void startLaunchBounce(const std::wstring& appId);
    void stopLaunchBounce(const std::wstring& appId);
    void setAttention(const std::wstring& appId, bool on);
    bool isBouncing(const std::wstring& appId) const { return bounces_.contains(appId); }

    // Hauteur réservée à l'écran (AppBar) et hauteur totale de la fenêtre, en pixels.
    static double reservePx(const Settings& s, const Metrics& m, float scale);
    static double windowHeightPx(const Settings& s, const Metrics& m, float scale);

private:
    struct Bounce {
        double elapsed = 0;
        bool attention = false;
        double stopAt = -1;   // fin du rebond en cours, puis arrêt
    };

    void refreshItems();
    LayoutResult layout() const;
    double bgBottomPx() const { return height_ - metrics_.dockScreenMargin * scale_; }
    double toPoints(LONG x) const { return (x - width_ / 2.0) / scale_; }
    double toPx(double points) const { return width_ / 2.0 + points * scale_; }
    double bounceOffset(const std::wstring& appId) const;   // en points
    std::optional<std::size_t> hoveredIndex() const;
    bool appRunning(const std::wstring& appId) const;

    Settings settings_;
    Metrics metrics_;
    AppModel* model_ = nullptr;
    std::vector<DockItem> items_;
    std::uint64_t revision_ = 0;
    double width_ = 0, height_ = 0;
    float scale_ = 1;
    Spring amount_;
    std::optional<double> cursor_;      // points sur l'axe principal
    bool cursorInside_ = false;
    std::map<std::wstring, Bounce> bounces_;
    double tooltipOpacity_ = 0;
    std::optional<std::size_t> tooltipIndex_;
    bool dirty_ = true;
};

} // namespace md
