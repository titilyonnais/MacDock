#include "dock_controller.h"

#include <algorithm>
#include <cmath>

#include "../anim/bounce.h"

namespace md {

namespace {
constexpr double kFitMarginPt = 4;   // marge laissée à chaque bout de l'axe quand le Dock doit rétrécir
} // namespace

void DockController::init(const Settings& s, const Metrics& m, AppModel* model) {
    model_ = model;
    setSettings(s);
    setMetrics(m);
    amount_.snap(0);
    refreshItems();
}

void DockController::setSettings(const Settings& s) {
    settings_ = s;
    edge_.edge = s.position;
    width_ = edge_.axis();
    height_ = edge_.cross();
    dirty_ = true;
}

void DockController::setMetrics(const Metrics& m) {
    metrics_ = m;
    amount_.setParams(m.magnifyStiffness, m.magnifyDamping);
    collapse_.setParams(m.dragStiffness, m.dragDamping);
    for (auto& [key, g] : gaps_) g.setParams(m.dragStiffness, m.dragDamping);
    dirty_ = true;
}

void DockController::setViewport(UINT width, UINT height, float scale) {
    // Fenêtre réelle ; les calculs se font dans le repère local (« comme en bas »).
    edge_ = EdgeFrame{settings_.position, double(width), double(height)};
    width_ = edge_.axis();
    height_ = edge_.cross();
    scale_ = scale > 0 ? scale : 1;
    dirty_ = true;
}

double DockController::reservePx(const Settings& s, const Metrics& m, float scale) {
    return std::ceil((s.tileSize + 2 * m.dockPadding + m.dockScreenMargin) * scale);
}

double DockController::windowHeightPx(const Settings& s, const Metrics& m, float scale) {
    double large = s.magnification ? std::max(s.largeSize, s.tileSize) : s.tileSize;
    // Dock vertical : l'infobulle se pose à côté de l'icône, il faut la place de sa largeur.
    double tooltip = s.position == DockPosition::Bottom ? m.tooltipFontSize * 1.6 + 2 * m.tooltipPadY : 240;
    double headroom = (large - s.tileSize) + m.attentionBounceHeight * s.tileSize + m.tooltipGap + tooltip + 8;
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

DockController::Laid DockController::layout() const {
    Laid out;
    LayoutInput in;
    in.items.reserve(items_.size() + gaps_.size());
    out.slot.resize(items_.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const DockItem& it = items_[i];
        if (auto g = gaps_.find(it.key); g != gaps_.end() && g->second.value() > 1e-3)
            in.items.push_back({false, true, g->second.value()});
        out.slot[i] = in.items.size();
        double presence = !collapsingKey_.empty() && it.key == collapsingKey_ ? collapse_.value() : 1.0;
        in.items.push_back({it.kind == ItemKind::Separator, false, presence});
    }
    in.tileSize = settings_.tileSize;
    in.largeSize = settings_.magnification ? std::max(settings_.largeSize, settings_.tileSize) : settings_.tileSize;
    in.gap = metrics_.iconGap;
    in.padding = metrics_.dockPadding;
    in.separatorWidth = metrics_.separatorWidth;
    in.separatorMargin = metrics_.separatorMargin;
    in.rangeTiles = metrics_.magnifyRangeTiles;
    // Trop d'éléments pour l'axe (Dock vertical sur un portable…) : tout le Dock rétrécit, cases, écarts et
    // marges ensemble, comme sur macOS — la longueur au repos est proportionnelle à ces mesures.
    const double room = width_ / scale_ - 2 * kFitMarginPt;
    if (const double rest = computeLayout(in).restLength; room > 0 && rest > room) {
        const double f = room / rest;
        in.tileSize *= f;
        in.largeSize *= f;
        in.gap *= f;
        in.padding *= f;
        in.separatorMargin *= f;
    }
    in.amount = amount_.value();
    in.cursor = cursor_;
    out.r = computeLayout(in);
    return out;
}

bool DockController::isInsideInteractiveZone(POINT p) const { return insideLocal(edge_.toLocal(p)); }

std::optional<std::size_t> DockController::hitTest(POINT p) const { return hitLocal(edge_.toLocal(p)); }

bool DockController::insideLocal(POINT p) const {
    if (width_ <= 0) return false;
    LayoutResult r = layout().r;
    double left = toPx(r.bgStart), right = toPx(r.bgEnd);
    double bottom = bgBottomPx();
    double top = bottom - r.thickness * scale_;
    if (amount_.value() > 0.01) top = std::min(top, bottom - (metrics_.dockPadding + r.maxSize) * scale_);
    const double y = p.y - hideOffsetPx(r);
    return p.x >= left && p.x <= right && y >= top && y <= height_;
}

void DockController::setShown(double shown) {
    if (shown == shown_) return;
    shown_ = shown;
    dirty_ = true;
}

double DockController::hideOffsetPx(const LayoutResult& r) const {
    if (shown_ >= 1) return 0;
    // Du haut du Dock au repos jusqu'au bas de la fenêtre, ombre comprise.
    const double restTop = bgBottomPx() - r.thickness * scale_;
    return (1 - shown_) * (height_ - restTop + 2 * metrics_.shadowBlur * scale_);
}

std::optional<std::size_t> DockController::hitLocal(POINT p) const {
    if (!insideLocal(p)) return std::nullopt;
    Laid l = layout();
    double x = toPoints(p.x);
    double halfGap = metrics_.iconGap / 2;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].kind == ItemKind::Separator) continue;
        if (drag_ && items_[i].key == collapsingKey_) continue;
        const LayoutItem& li = l.r.items[l.slot[i]];
        double half = li.size / 2 + halfGap;
        if (x >= li.center - half && x <= li.center + half) return i;
    }
    return std::nullopt;
}

std::optional<std::size_t> DockController::hitTestAny(POINT window) const {
    const POINT p = edge_.toLocal(window);
    if (auto hit = hitLocal(p)) return hit;
    if (!insideLocal(p)) return std::nullopt;
    Laid l = layout();
    double x = toPoints(p.x);
    // Le séparateur occupe son trait et ses marges.
    double half = metrics_.separatorWidth / 2 + metrics_.separatorMargin;
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].kind == ItemKind::Separator && std::fabs(x - l.r.items[l.slot[i]].center) <= half) return i;
    return std::nullopt;
}

const DockItem* DockController::itemAt(std::size_t index) const {
    return index < items_.size() ? &items_[index] : nullptr;
}

std::optional<std::size_t> DockController::hoveredIndex() const {
    if (drag_ || !cursorInside_ || !cursor_) return std::nullopt;
    POINT p{LONG(std::lround(toPx(*cursor_))), LONG(std::lround(bgBottomPx() - 1))};
    return hitLocal(p);
}

void DockController::setCursor(std::optional<POINT> window) {
    refreshItems();
    std::optional<POINT> clientPx;
    if (window) clientPx = edge_.toLocal(*window);
    bool inside = clientPx && insideLocal(*clientPx);
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

    if (!collapsingKey_.empty()) {
        if (collapse_.step(dt)) animating = true;
        if (!drag_ && collapse_.settled() && collapse_.value() > 0.999) collapsingKey_.clear();
        else animating = true;
    }
    for (auto it = gaps_.begin(); it != gaps_.end();) {
        if (it->second.step(dt)) animating = true;
        if (it->second.target() == 0 && it->second.settled() && it->second.value() < 1e-3) it = gaps_.erase(it);
        else ++it;
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

IconProvider::ImagePtr DockController::imageFor(const DockItem& item, IconProvider& icons, int px) const {
    switch (item.kind) {
        case ItemKind::App: {
            std::wstring parsing = item.launch;
            if (parsing.empty() && model_)
                if (auto id = model_->identityOf(item.appId)) parsing = id->exePath;
            return icons.get(item.appId, parsing, px);
        }
        case ItemKind::AppsButton: return icons.appsButton(px);
        case ItemKind::Stack: return icons.get(L"stack:" + item.launch, item.launch, px);
        case ItemKind::Trash: return icons.trash(item.trashFull, px);
        case ItemKind::MinimizedWindow: {
            std::wstring parsing;
            if (model_)
                if (auto id = model_->identityOf(item.appId)) parsing = id->launch.empty() ? id->exePath : id->launch;
            return icons.get(item.appId, parsing, px);
        }
        default: return nullptr;
    }
}

std::optional<std::size_t> DockController::indexOfKey(const std::wstring& key) const {
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].key == key) return i;
    return std::nullopt;
}

namespace {
bool inSection(const DockItem& it, bool stacks) {
    if (stacks) return it.kind == ItemKind::Stack;
    return (it.kind == ItemKind::App && it.pinned) || it.kind == ItemKind::AppsButton;
}
} // namespace

std::vector<std::size_t> DockController::candidates(Section section, const std::wstring& exclude) const {
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (items_[i].key != exclude && inSection(items_[i], section == Section::Stacks)) out.push_back(i);
    return out;
}

std::wstring DockController::gapKey(Section section, std::size_t slot) const {
    auto c = candidates(section, drag_ ? drag_->key : std::wstring());
    if (slot < c.size()) return items_[c[slot]].key;
    // Fin de section : devant l'élément qui suit le dernier membre (élément tiré compris).
    std::optional<std::size_t> last;
    for (std::size_t i = 0; i < items_.size(); ++i)
        if (inSection(items_[i], section == Section::Stacks) || (drag_ && items_[i].key == drag_->key)) last = i;
    if (last && *last + 1 < items_.size()) return items_[*last + 1].key;
    return items_.empty() ? std::wstring() : items_.front().key;
}

std::optional<std::size_t> DockController::insertionPinnedIndex(Section section, std::size_t slot) const {
    if (!drag_) return std::nullopt;
    return insertionIndex(section, slot, drag_->key);
}

std::optional<std::size_t> DockController::insertionIndex(Section section, std::size_t slot,
                                                          const std::wstring& exclude) const {
    if (!model_) return std::nullopt;
    auto c = candidates(section, exclude);
    if (slot < c.size()) return model_->pinnedIndexOf(items_[c[slot]].key);
    if (!c.empty())
        if (auto i = model_->pinnedIndexOf(items_[c.back()].key)) return *i + 1;
    if (!exclude.empty())
        if (auto self = model_->pinnedIndexOf(exclude)) return self;
    if (section == Section::Stacks) return model_->pinnedEntries().size();
    return std::size_t(0);
}

LayoutResult DockController::stableLayout() const {
    // Positions sans place ouverte ni repli : cibles stables pendant que les icônes glissent.
    LayoutInput in;
    for (auto& it : items_) in.items.push_back({it.kind == ItemKind::Separator});
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

bool DockController::overPinnedSection(double xPx, const std::wstring& exclude) const {
    auto c = candidates(Section::Pinned, exclude);
    if (c.empty()) return true;
    LayoutResult r = stableLayout();
    const LayoutItem& last = r.items[c.back()];
    return toPoints(LONG(std::lround(xPx))) <= last.center + last.size;   // jusqu'à une demi-case après la dernière
}

std::size_t DockController::slotAt(Section section, const std::wstring& exclude, double xPx) const {
    LayoutResult r = stableLayout();
    double x = toPoints(LONG(std::lround(xPx)));
    std::size_t slot = 0;
    for (std::size_t i : candidates(section, exclude))
        if (r.items[i].center < x) ++slot;
    return slot;
}

void DockController::openGap(const std::wstring& target) {
    for (auto& [key, g] : gaps_) g.setTarget(key == target ? 1.0 : 0.0);
    if (!target.empty() && !gaps_.contains(target)) {
        Spring g(metrics_.dragStiffness, metrics_.dragDamping);
        g.snap(0);
        g.setTarget(1);
        gaps_.emplace(target, g);
    }
    dirty_ = true;
}

DropHover DockController::dropOver(POINT window, const std::vector<std::wstring>& paths) {
    refreshItems();
    const POINT p = edge_.toLocal(window);
    DropHover h;
    std::optional<std::size_t> item;
    bool between = false;
    if (insideLocal(p)) {
        item = hitLocal(p);
        Laid l = layout();
        const double x = toPoints(p.x);
        // Un dépôt vise l'icône elle-même, pas l'intervalle qui la sépare de sa voisine.
        const bool onIcon = item && std::abs(x - l.r.items[l.slot[*item]].center) <= l.r.items[l.slot[*item]].size / 2;
        const bool singlePin = paths.size() == 1 && isPinnableFile(paths[0]);
        if (singlePin) {
            // Entre deux icônes épinglées : hors du centre d'une icône de la section, ou dans un intervalle.
            auto c = candidates(Section::Pinned, L"");
            if (!c.empty()) {
                const LayoutItem& first = l.r.items[l.slot[c.front()]];
                const LayoutItem& last = l.r.items[l.slot[c.back()]];
                bool inSpan = x >= first.center - first.size && x <= last.center + last.size;
                bool pinnedItem = item && std::find(c.begin(), c.end(), *item) != c.end();
                if (pinnedItem) {
                    const LayoutItem& li = l.r.items[l.slot[*item]];
                    between = std::abs(x - li.center) > li.size * 0.25;
                } else {
                    between = !item && inSpan;
                }
            }
        }
        if (!between && !onIcon) item.reset();
        DropTargetInfo info;
        info.betweenPinned = between;
        if (item) info.kind = items_[*item].kind;
        h.action = (between || item) ? dropAction(info, paths) : DropAction::None;
    }
    if (h.action == DropAction::Pin) {
        std::size_t slot = slotAt(Section::Pinned, L"", double(p.x));
        h.pinIndex = insertionIndex(Section::Pinned, slot, L"").value_or(0);
        openGap(gapKey(Section::Pinned, slot));
        dropItem_.reset();
    } else {
        openGap(L"");
        if (h.action != DropAction::None) h.item = item;
        if (dropItem_ != h.item) dirty_ = true;
        dropItem_ = h.item;
    }
    return h;
}

void DockController::dropLeave() {
    openGap(L"");
    dropItem_.reset();
    dirty_ = true;
}

void DockController::updateDragTarget(POINT p) {
    if (!drag_) return;
    DragState& d = *drag_;
    double bgTop = bgBottomPx() - (settings_.tileSize + 2 * metrics_.dockPadding) * scale_;
    bool above = bgTop - p.y > metrics_.dragRemoveDistance * scale_;
    d.removing = above && d.pinned;
    d.hasSlot = !above;
    // Une app ouverte non épinglée ne s'épingle que lâchée sur la section épinglée (sinon : retour à sa place).
    if (d.hasSlot && d.kind == ItemKind::App && !d.pinned && !overPinnedSection(double(p.x), d.key)) d.hasSlot = false;
    if (d.hasSlot) d.slot = slotAt(d.section, d.key, double(p.x));
    openGap(d.hasSlot ? gapKey(d.section, d.slot) : std::wstring());
}

void DockController::pointerDown(POINT window) {
    refreshItems();
    const POINT p = edge_.toLocal(window);
    pressPoint_ = p;
    pressIndex_ = hitLocal(p);
    drag_.reset();
    dirty_ = true;   // icône pressée assombrie
}

void DockController::pointerMove(POINT window) {
    const POINT p = edge_.toLocal(window);
    if (drag_) {
        updateDragTarget(p);
        return;
    }
    if (!pressPoint_ || !pressIndex_ || *pressIndex_ >= items_.size()) return;
    double dist = std::hypot(double(p.x - pressPoint_->x), double(p.y - pressPoint_->y)) / scale_;
    if (dist < metrics_.dragThreshold) return;
    const DockItem& item = items_[*pressIndex_];
    bool draggable = item.kind == ItemKind::App || item.kind == ItemKind::AppsButton || item.kind == ItemKind::Stack;
    if (!draggable) return;
    Laid l = layout();
    DragState d;
    d.key = item.key;
    d.appId = item.appId;
    d.kind = item.kind;
    d.pinned = item.pinned;
    d.running = item.running;
    d.section = item.kind == ItemKind::Stack ? Section::Stacks : Section::Pinned;
    d.pickupSize = std::max(settings_.tileSize, l.r.items[l.slot[*pressIndex_]].size);
    drag_ = d;
    collapsingKey_ = item.key;
    collapse_.setParams(metrics_.dragStiffness, metrics_.dragDamping);
    collapse_.snap(1);
    collapse_.setTarget(0);
    gaps_.clear();
    tooltipOpacity_ = 0;
    updateDragTarget(p);
}

DragOutcome DockController::pointerUp(POINT window) {
    const POINT p = edge_.toLocal(window);
    DragOutcome o;
    if (drag_) {
        updateDragTarget(p);
        DragState d = *drag_;
        if (d.removing) {
            o.kind = DragOutcome::Kind::Remove;
            o.key = d.key;
            o.poof = !d.running;
        } else if (d.hasSlot) {
            auto before = insertionPinnedIndex(d.section, d.slot);
            if (d.pinned && model_) {
                auto from = model_->pinnedIndexOf(d.key);
                if (from && before) {
                    std::size_t to = *before > *from ? *before - 1 : *before;
                    if (to != *from) {
                        o.kind = DragOutcome::Kind::Move;
                        o.fromPinned = *from;
                        o.toPinned = to;
                    }
                }
            } else if (d.kind == ItemKind::App && before) {
                o.kind = DragOutcome::Kind::Pin;
                o.appId = d.appId;
                o.toPinned = *before;
            }
        }
        drag_.reset();
        if (o.kind == DragOutcome::Kind::None) {
            collapse_.setTarget(1);   // retour à sa place
            for (auto& [key, g] : gaps_) g.setTarget(0);
        } else {
            collapsingKey_.clear();   // l'appelant applique le changement : la place ouverte devient l'élément
            collapse_.snap(1);
            gaps_.clear();
        }
    } else if (pressIndex_ && hitLocal(p) == pressIndex_) {
        o.kind = DragOutcome::Kind::Click;
        o.index = *pressIndex_;
    }
    pressPoint_.reset();
    pressIndex_.reset();
    dirty_ = true;
    return o;
}

void DockController::cancelDrag() {
    pressPoint_.reset();
    pressIndex_.reset();
    dirty_ = true;
    if (!drag_) return;
    drag_.reset();
    collapse_.setTarget(1);
    for (auto& [key, g] : gaps_) g.setTarget(0);
    dirty_ = true;
}

DragVisual DockController::dragVisual(IconProvider& icons) const {
    DragVisual v;
    if (!drag_) return v;
    auto i = indexOfKey(drag_->key);
    if (!i) return v;
    v.active = true;
    v.sizePx = float(drag_->pickupSize * scale_);
    v.image = imageFor(items_[*i], icons, int(std::ceil(v.sizePx)));
    v.removing = drag_->removing && !drag_->running;
    v.key = drag_->key;
    return v;
}

RenderFrame DockController::buildFrame(bool dark, IconProvider& icons) {
    refreshItems();
    Laid laid = layout();
    const LayoutResult& r = laid.r;
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
        const LayoutItem& li = r.items[laid.slot[i]];
        icon.cx = float(toPx(li.center));
        if (item.kind == ItemKind::Separator) {
            icon.separator = true;
            icon.cy = (f.bgTop + f.bgBottom) / 2;
            icon.sepLength = float(g.separatorLength) * s;
            f.icons.push_back(icon);
            continue;
        }
        icon.size = float(li.size) * s;
        icon.cy = f.bgBottom - float(metrics_.dockPadding) * s - icon.size / 2 - float(bounceOffset(item.appId)) * s;
        icon.indicatorY = f.bgBottom - float(g.indicatorCenter) * s;
        icon.image = imageFor(item, icons, imgPx);
        icon.indicator = item.kind == ItemKind::App && item.running;
        if (item.kind == ItemKind::MinimizedWindow) icon.window = item.window;
        if ((dropItem_ && *dropItem_ == i) || (pressIndex_ && *pressIndex_ == i && !drag_)) icon.dim = 0.3f;
        if (!collapsingKey_.empty() && item.key == collapsingKey_) {
            // Élément tiré : sa case se vide (il suit le curseur) puis réapparaît s'il revient à sa place.
            icon.opacity = drag_ ? 0.0f : float(std::clamp(collapse_.value(), 0.0, 1.0));
            icon.indicator = icon.indicator && icon.opacity > 0.5f;
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
    // Masquage automatique : tout le Dock glisse vers le bas, hors de la fenêtre.
    if (const float off = float(hideOffsetPx(r)); off > 0) {
        f.bgTop += off;
        f.bgBottom += off;
        for (auto& icon : f.icons) {
            icon.cy += off;
            icon.indicatorY += off;
        }
        f.tooltip.bottom += off;
    }
    for (auto& icon : f.icons) icon.indicatorX = icon.cx;
    if (edge_.vertical()) toWindow(f);
    dirty_ = false;
    return f;
}

// Repère local → fenêtre réelle (Dock à gauche ou à droite).
void DockController::toWindow(RenderFrame& f) const {
    auto a = edge_.toWindow(f.bgLeft, f.bgTop), b = edge_.toWindow(f.bgRight, f.bgBottom);
    f.bgLeft = float(std::min(a.x, b.x));
    f.bgRight = float(std::max(a.x, b.x));
    f.bgTop = float(std::min(a.y, b.y));
    f.bgBottom = float(std::max(a.y, b.y));
    for (auto& icon : f.icons) {
        auto c = edge_.toWindow(icon.cx, icon.cy);
        auto dot = edge_.toWindow(icon.indicatorX, icon.indicatorY);
        icon.cx = float(c.x);
        icon.cy = float(c.y);
        icon.indicatorX = float(dot.x);
        icon.indicatorY = float(dot.y);
        icon.sepHorizontal = icon.separator;
    }
    auto t = edge_.toWindow(f.tooltip.cx, f.tooltip.bottom);
    f.tooltip.cx = float(t.x);
    f.tooltip.bottom = float(t.y);
    f.tooltip.side = edge_.edge == DockPosition::Left ? TooltipSide::Right : TooltipSide::Left;
}

} // namespace md
