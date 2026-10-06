#include "dock_controller.h"

#include <algorithm>
#include <cmath>

#include "../anim/bounce.h"

namespace md {

namespace {
constexpr wchar_t kRecycleBin[] = L"shell:RecycleBinFolder";
}

void DockController::init(const Settings& s, const Metrics& m, AppModel* model) {
    model_ = model;
    setSettings(s);
    setMetrics(m);
    amount_.snap(0);
    refreshItems();
}

void DockController::setSettings(const Settings& s) {
    settings_ = s;
    dirty_ = true;
}

void DockController::setMetrics(const Metrics& m) {
    metrics_ = m;
    amount_.setParams(m.magnifyStiffness, m.magnifyDamping);
    dirty_ = true;
}

void DockController::setViewport(UINT width, UINT height, float scale) {
    width_ = width;
    height_ = height;
    scale_ = scale > 0 ? scale : 1;
    dirty_ = true;
}

double DockController::reservePx(const Settings& s, const Metrics& m, float scale) {
    return std::ceil((s.tileSize + 2 * m.dockPadding + m.dockScreenMargin) * scale);
}

double DockController::windowHeightPx(const Settings& s, const Metrics& m, float scale) {
    double large = s.magnification ? std::max(s.largeSize, s.tileSize) : s.tileSize;
    double headroom = (large - s.tileSize) + m.attentionBounceHeight * s.tileSize + m.tooltipGap +
                      m.tooltipFontSize * 1.6 + 2 * m.tooltipPadY + 8;
    return reservePx(s, m, scale) + std::ceil(headroom * scale);
}

void DockController::refreshItems() {
    if (!model_ || model_->revision() == revision_) return;
    revision_ = model_->revision();
    items_ = model_->items();
    if (tooltipIndex_ && *tooltipIndex_ >= items_.size()) tooltipIndex_.reset();
    dirty_ = true;
}

bool DockController::appRunning(const std::wstring& appId) const {
    return std::any_of(items_.begin(), items_.end(),
                       [&](auto& i) { return i.kind == ItemKind::App && i.appId == appId && i.running; });
}

LayoutResult DockController::layout() const {
    LayoutInput in;
    in.items.reserve(items_.size());
    for (auto& i : items_) in.items.push_back({i.kind == ItemKind::Separator});
    in.tileSize = settings_.tileSize;
    in.largeSize = settings_.magnification ? std::max(settings_.largeSize, settings_.tileSize) : settings_.tileSize;
    in.gap = metrics_.iconGap;
    in.padding = metrics_.dockPadding;
    in.separatorWidth = metrics_.separatorWidth;
    in.separatorMargin = metrics_.separatorMargin;
    in.rangeTiles = metrics_.magnifyRangeTiles;
    in.amount = amount_.value();
    in.cursor = cursor_;
    return computeLayout(in);
}

bool DockController::isInsideInteractiveZone(POINT p) const {
    if (width_ <= 0) return false;
    LayoutResult r = layout();
    double left = toPx(r.bgStart), right = toPx(r.bgEnd);
    double bottom = bgBottomPx();
    double top = bottom - r.thickness * scale_;
    if (amount_.value() > 0.01) top = std::min(top, bottom - (metrics_.dockPadding + r.maxSize) * scale_);
    return p.x >= left && p.x <= right && p.y >= top && p.y <= height_;
}

std::optional<std::size_t> DockController::hitTest(POINT p) const {
    if (!isInsideInteractiveZone(p)) return std::nullopt;
    LayoutResult r = layout();
    double x = toPoints(p.x);
    double halfGap = metrics_.iconGap / 2;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].kind == ItemKind::Separator) continue;
        double half = r.items[i].size / 2 + halfGap;
        if (x >= r.items[i].center - half && x <= r.items[i].center + half) return i;
    }
    return std::nullopt;
}

const DockItem* DockController::itemAt(std::size_t index) const {
    return index < items_.size() ? &items_[index] : nullptr;
}

std::optional<std::size_t> DockController::hoveredIndex() const {
    if (!cursorInside_ || !cursor_) return std::nullopt;
    POINT p{LONG(std::lround(toPx(*cursor_))), LONG(std::lround(bgBottomPx() - 1))};
    return hitTest(p);
}

void DockController::setCursor(std::optional<POINT> clientPx) {
    refreshItems();
    bool inside = clientPx && isInsideInteractiveZone(*clientPx);
    if (inside) {
        double c = toPoints(clientPx->x);
        if (!cursorInside_ || !cursor_ || *cursor_ != c) dirty_ = true;
        cursor_ = c;
        cursorInside_ = true;
        amount_.setTarget(settings_.magnification ? 1.0 : 0.0);
    } else {
        if (cursorInside_) dirty_ = true;
        cursorInside_ = false;   // cursor_ est conservé pendant que la magnification retombe
        amount_.setTarget(0.0);
    }
}

void DockController::startLaunchBounce(const std::wstring& appId) {
    if (appId.empty() || bounces_.contains(appId)) return;
    bounces_[appId] = Bounce{};
    dirty_ = true;
}

void DockController::stopLaunchBounce(const std::wstring& appId) {
    auto it = bounces_.find(appId);
    if (it == bounces_.end() || it->second.attention || it->second.stopAt >= 0) return;
    double period = metrics_.launchBouncePeriod;
    it->second.stopAt = std::ceil(it->second.elapsed / period) * period;
}

void DockController::setAttention(const std::wstring& appId, bool on) {
    if (appId.empty()) return;
    auto it = bounces_.find(appId);
    if (on) {
        if (it == bounces_.end() || !it->second.attention) bounces_[appId] = Bounce{0, true, -1};
        dirty_ = true;
    } else if (it != bounces_.end() && it->second.attention && it->second.stopAt < 0) {
        double period = metrics_.attentionBouncePeriod;
        double active = period * std::max(1.0, metrics_.attentionBounceCount);
        double cycle = active + std::max(0.0, metrics_.attentionPause);
        double t = std::fmod(it->second.elapsed, cycle);
        // Pendant la pause : arrêt immédiat ; sinon on finit le rebond en cours.
        it->second.stopAt = t >= active ? it->second.elapsed : std::ceil(it->second.elapsed / period) * period;
    }
}

double DockController::bounceOffset(const std::wstring& appId) const {
    auto it = bounces_.find(appId);
    if (it == bounces_.end()) return 0;
    const Bounce& b = it->second;
    if (b.stopAt >= 0 && b.elapsed >= b.stopAt) return 0;
    double tile = settings_.tileSize;
    if (b.attention)
        return attentionBounceOffset(b.elapsed, metrics_.attentionBouncePeriod, metrics_.attentionBounceHeight * tile,
                                     int(metrics_.attentionBounceCount), metrics_.attentionPause);
    return launchBounceOffset(b.elapsed, metrics_.launchBouncePeriod, metrics_.launchBounceHeight * tile);
}

bool DockController::tick(double dt) {
    refreshItems();
    bool animating = amount_.step(dt);

    for (auto it = bounces_.begin(); it != bounces_.end();) {
        Bounce& b = it->second;
        b.elapsed += dt;
        if (b.attention && b.stopAt < 0 && !appRunning(it->first)) b.stopAt = b.elapsed;   // app fermée
        if (!b.attention && b.stopAt < 0 && (appRunning(it->first) || b.elapsed >= metrics_.launchTimeout)) {
            double period = metrics_.launchBouncePeriod;
            b.stopAt = std::ceil(b.elapsed / period) * period;
        }
        if (b.stopAt >= 0 && b.elapsed >= b.stopAt) it = bounces_.erase(it);
        else ++it;
        animating = true;
    }

    auto hovered = hoveredIndex();
    if (hovered) tooltipIndex_ = hovered;
    double target = hovered ? 1.0 : 0.0;
    double step = metrics_.tooltipFadeSeconds > 0 ? dt / metrics_.tooltipFadeSeconds : 1.0;
    if (tooltipOpacity_ != target) {
        tooltipOpacity_ = target > tooltipOpacity_ ? std::min(target, tooltipOpacity_ + step)
                                                   : std::max(target, tooltipOpacity_ - step);
        animating = true;
    }
    if (animating) dirty_ = true;
    return animating;
}

bool DockController::consumeDirty() {
    bool d = dirty_;
    dirty_ = false;
    return d;
}

RenderFrame DockController::buildFrame(bool dark, IconProvider& icons) {
    refreshItems();
    LayoutResult r = layout();
    const float s = scale_;
    RenderFrame f;
    f.scale = s;
    f.dark = dark;
    f.position = settings_.position;
    f.bgLeft = float(toPx(r.bgStart));
    f.bgRight = float(toPx(r.bgEnd));
    f.bgBottom = float(bgBottomPx());
    f.bgTop = f.bgBottom - float(r.thickness) * s;
    const DockGeometry g = dockGeometry(settings_.tileSize, metrics_);
    f.cornerRadius = float(g.cornerRadius) * s;

    double large = settings_.magnification ? std::max(settings_.largeSize, settings_.tileSize) : settings_.tileSize;
    int imgPx = int(std::ceil(large * s));

    for (std::size_t i = 0; i < items_.size(); ++i) {
        const DockItem& item = items_[i];
        RenderIcon icon;
        icon.cx = float(toPx(r.items[i].center));
        if (item.kind == ItemKind::Separator) {
            icon.separator = true;
            icon.cy = (f.bgTop + f.bgBottom) / 2;
            icon.sepLength = float(g.separatorLength) * s;
            f.icons.push_back(icon);
            continue;
        }
        icon.size = float(r.items[i].size) * s;
        icon.cy = f.bgBottom - float(metrics_.dockPadding) * s - icon.size / 2 - float(bounceOffset(item.appId)) * s;
        icon.indicatorY = f.bgBottom - float(g.indicatorCenter) * s;
        switch (item.kind) {
            case ItemKind::App: {
                std::wstring parsing = item.launch;
                if (parsing.empty())
                    if (auto id = model_->identityOf(item.appId)) parsing = id->exePath;
                icon.image = icons.get(item.appId, parsing, imgPx);
                icon.indicator = item.running;
                break;
            }
            case ItemKind::AppsButton: icon.image = icons.appsButton(imgPx); break;
            case ItemKind::Stack: icon.image = icons.get(L"stack:" + item.launch, item.launch, imgPx); break;
            case ItemKind::Trash: icon.image = icons.get(L"trash", kRecycleBin, imgPx); break;
            case ItemKind::MinimizedWindow: {
                std::wstring parsing;
                if (auto id = model_->identityOf(item.appId)) parsing = id->launch.empty() ? id->exePath : id->launch;
                icon.image = icons.get(item.appId, parsing, imgPx);
                break;
            }
            default: break;
        }
        f.icons.push_back(std::move(icon));
    }

    if (tooltipIndex_ && *tooltipIndex_ < items_.size() && tooltipOpacity_ > 0) {
        const DockItem& item = items_[*tooltipIndex_];
        const RenderIcon& icon = f.icons[*tooltipIndex_];
        f.tooltip.visible = true;
        f.tooltip.text = item.name;
        if (item.kind == ItemKind::Trash) f.tooltip.text = L"Corbeille";
        else if (item.kind == ItemKind::AppsButton && f.tooltip.text.empty()) f.tooltip.text = L"Apps";
        f.tooltip.cx = icon.cx;
        // Au-dessus de la forme visible (la case contient la marge transparente de la grille Apple).
        float visibleTop = icon.cy - icon.size / 2 + icon.size * float(1 - metrics_.iconShapeRatio) / 2;
        f.tooltip.bottom = visibleTop - float(metrics_.tooltipGap) * s;
        f.tooltip.opacity = float(tooltipOpacity_);
    }
    dirty_ = false;
    return f;
}

} // namespace md
