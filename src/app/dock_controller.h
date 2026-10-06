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

// Résultat d'un appui suivi d'un relâchement (clic ou glisser-déposer).
struct DragOutcome {
    enum class Kind { None, Click, Move, Pin, Remove } kind = Kind::None;
    std::size_t index = 0;                  // Click : index dans les éléments affichés
    std::size_t fromPinned = 0, toPinned = 0;   // Move : index de pinnedEntries() ; Pin : toPinned
    std::wstring key, appId;                // Remove : clé de l'épingle ; Pin : appId
    bool poof = false;                      // Remove : l'élément quitte le Dock (nuage)
};

// Icône en cours de glisser, à dessiner sous le curseur (fenêtre de sprite).
struct DragVisual {
    bool active = false;
    IconProvider::ImagePtr image;
    float sizePx = 0;
    bool removing = false;   // au-dessus du seuil : étiquette « Supprimer »
    std::wstring key;
};

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

    // Glisser-déposer interne. pointerMove reçoit aussi les positions hors du Dock (capture de la souris).
    void pointerDown(POINT clientPx);
    void pointerMove(POINT clientPx);
    DragOutcome pointerUp(POINT clientPx);
    void cancelDrag();
    bool dragging() const { return drag_.has_value(); }
    DragVisual dragVisual(IconProvider& icons) const;

    std::optional<std::size_t> hitTest(POINT clientPx) const;
    // Comme hitTest, mais reconnaît aussi les séparateurs (clic droit : menu du Dock).
    std::optional<std::size_t> hitTestAny(POINT clientPx) const;
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

    enum class Section { Pinned, Stacks };
    struct DragState {
        std::wstring key, appId;
        ItemKind kind = ItemKind::App;
        bool pinned = false, running = false;
        Section section = Section::Pinned;
        double pickupSize = 0;          // points
        bool removing = false;
        std::size_t slot = 0;           // position d'insertion parmi les candidats de la section
        bool hasSlot = false;
    };
    // Mise en page avec les emplacements vides du glisser ; slot[i] = index dans r.items de items_[i].
    struct Laid {
        LayoutResult r;
        std::vector<std::size_t> slot;
    };

    void refreshItems();
    Laid layout() const;
    IconProvider::ImagePtr imageFor(const DockItem& item, IconProvider& icons, int px) const;
    std::vector<std::size_t> candidates(Section section, const std::wstring& exclude) const;
    std::wstring gapKey(Section section, std::size_t slot) const;   // clé de l'élément devant lequel s'ouvre la place
    std::optional<std::size_t> insertionPinnedIndex(Section section, std::size_t slot) const;
    void updateDragTarget(POINT clientPx);
    std::optional<std::size_t> indexOfKey(const std::wstring& key) const;
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

    std::optional<POINT> pressPoint_;
    std::optional<std::size_t> pressIndex_;
    std::optional<DragState> drag_;
    std::wstring collapsingKey_;           // élément replié (glisser en cours ou retour animé)
    Spring collapse_;                      // présence de collapsingKey_
    std::map<std::wstring, Spring> gaps_;  // places ouvertes devant l'élément de clé donnée
};

} // namespace md
