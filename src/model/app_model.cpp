#include "app_model.h"

#include <algorithm>

#include "../core/strings.h"

namespace md {

namespace {
constexpr std::size_t kMaxRecents = 3;
}

std::wstring makeAppId(const std::wstring& aumid, const std::wstring& exePath) {
    return aumid.empty() ? toLower(exePath) : aumid;
}

std::wstring AppModel::pinKey(const PinnedEntry& p) {
    switch (p.kind) {
        case PinKind::AppsButton: return L"apps";
        case PinKind::Stack: return L"stack:" + p.launch;
        default: return L"app:" + p.appId;
    }
}

bool AppModel::isPinned(const std::wstring& appId) const {
    return std::any_of(pinned_.begin(), pinned_.end(),
                       [&](auto& p) { return p.kind == PinKind::App && p.appId == appId; });
}

void AppModel::loadPinned(const std::vector<PinnedEntry>& pins) {
    pinned_ = pins;
    std::erase_if(recents_, [&](auto& r) { return isPinned(r.appId); });
    touch();
}

void AppModel::setShowRecents(bool show) {
    if (showRecents_ == show) return;
    showRecents_ = show;
    touch();
}

void AppModel::windowOpened(WindowId id, const AppIdentity& original) {
    if (windows_.contains(id)) return;
    // Fenêtre sans l'AUMID de son épingle : on la rattache à l'épingle du même exécutable.
    AppIdentity app = original;
    if (!isPinned(app.appId) && !apps_.contains(app.appId) && !app.exePath.empty()) {
        std::wstring exe = toLower(app.exePath);
        for (auto& p : pinned_) {
            if (p.kind == PinKind::App && !p.exePath.empty() && toLower(p.exePath) == exe) {
                app.appId = p.appId;
                if (app.launch.empty()) app.launch = p.launch;
                break;
            }
        }
    }
    windows_[id] = Window{app.appId, {}, false, 0};
    auto& a = apps_[app.appId];
    if (a.windows.empty()) {
        a.identity = app;
        a.openSeq = ++seq_;
    }
    a.windows.push_back(id);
    a.hidden = false;
    std::erase_if(recents_, [&](auto& r) { return r.appId == app.appId; });
    touch();
}

void AppModel::windowClosed(WindowId id) {
    auto w = windows_.find(id);
    if (w == windows_.end()) return;
    std::wstring appId = w->second.appId;
    windows_.erase(w);
    auto a = apps_.find(appId);
    if (a != apps_.end()) {
        std::erase(a->second.windows, id);
        if (a->second.windows.empty()) {
            AppIdentity identity = a->second.identity;
            apps_.erase(a);
            if (!isPinned(appId)) {
                recents_.push_front(identity);
                if (recents_.size() > kMaxRecents) recents_.pop_back();
            }
        }
    }
    touch();
}

void AppModel::windowMinimized(WindowId id, bool minimized) {
    auto w = windows_.find(id);
    if (w == windows_.end() || w->second.minimized == minimized) return;
    w->second.minimized = minimized;
    w->second.minimizedSeq = minimized ? ++seq_ : 0;
    if (!minimized)
        if (auto a = apps_.find(w->second.appId); a != apps_.end()) a->second.hidden = false;
    touch();
}

void AppModel::setHidden(const std::wstring& appId, bool hidden) {
    auto a = apps_.find(appId);
    if (a == apps_.end() || a->second.hidden == hidden) return;
    a->second.hidden = hidden;
    touch();
}

void AppModel::setTrashFull(bool full) {
    if (trashFull_ == full) return;
    trashFull_ = full;
    touch();
}

bool AppModel::isHidden(const std::wstring& appId) const {
    auto a = apps_.find(appId);
    return a != apps_.end() && a->second.hidden;
}

void AppModel::windowTitle(WindowId id, const std::wstring& title) {
    auto w = windows_.find(id);
    if (w == windows_.end() || w->second.title == title) return;
    w->second.title = title;
    touch();
}

bool AppModel::pin(const std::wstring& appId, std::size_t index) {
    if (isPinned(appId)) return false;
    auto identity = identityOf(appId);
    if (!identity) return false;
    PinnedEntry e{PinKind::App, appId, identity->launch, identity->displayName, identity->exePath};
    index = std::min(index, pinned_.size());
    pinned_.insert(pinned_.begin() + std::ptrdiff_t(index), std::move(e));
    std::erase_if(recents_, [&](auto& r) { return r.appId == appId; });
    touch();
    return true;
}

bool AppModel::unpin(const std::wstring& key) {
    auto it = std::find_if(pinned_.begin(), pinned_.end(), [&](auto& p) { return pinKey(p) == key; });
    if (it == pinned_.end()) return false;
    pinned_.erase(it);
    touch();
    return true;
}

bool AppModel::movePinned(std::size_t from, std::size_t to) {
    if (from >= pinned_.size() || to >= pinned_.size()) return false;
    if (from == to) return true;
    PinnedEntry e = pinned_[from];
    pinned_.erase(pinned_.begin() + std::ptrdiff_t(from));
    pinned_.insert(pinned_.begin() + std::ptrdiff_t(to), std::move(e));
    touch();
    return true;
}

bool AppModel::setStackOptions(const std::wstring& key, StackView view, StackSort sort) {
    auto i = pinnedIndexOf(key);
    if (!i || pinned_[*i].kind != PinKind::Stack) return false;
    pinned_[*i].stackView = view;
    pinned_[*i].stackSort = sort;
    touch();
    return true;
}

std::optional<std::size_t> AppModel::pinnedIndexOf(const std::wstring& key) const {
    for (std::size_t i = 0; i < pinned_.size(); ++i)
        if (pinKey(pinned_[i]) == key) return i;
    return std::nullopt;
}

std::vector<WindowId> AppModel::windowsOf(const std::wstring& appId) const {
    auto a = apps_.find(appId);
    return a == apps_.end() ? std::vector<WindowId>{} : a->second.windows;
}

std::optional<AppIdentity> AppModel::identityOf(const std::wstring& appId) const {
    if (auto a = apps_.find(appId); a != apps_.end()) return a->second.identity;
    for (auto& r : recents_)
        if (r.appId == appId) return r;
    for (auto& p : pinned_)
        if (p.kind == PinKind::App && p.appId == appId) return AppIdentity{p.appId, p.exePath, {}, p.name, p.launch};
    return std::nullopt;
}

std::wstring AppModel::titleOf(WindowId id) const {
    auto w = windows_.find(id);
    return w == windows_.end() ? std::wstring() : w->second.title;
}

std::wstring AppModel::appOfWindow(WindowId id) const {
    auto w = windows_.find(id);
    return w == windows_.end() ? std::wstring() : w->second.appId;
}

std::vector<DockItem> AppModel::items() const {
    std::vector<DockItem> out;
    auto appItem = [&](const std::wstring& appId, const std::wstring& name, const std::wstring& launch) {
        DockItem d;
        d.kind = ItemKind::App;
        d.key = L"app:" + appId;
        d.appId = appId;
        d.name = name;
        d.launch = launch;
        if (auto a = apps_.find(appId); a != apps_.end()) {
            d.running = true;
            d.windows = a->second.windows;
            if (d.name.empty()) d.name = a->second.identity.displayName;
        }
        return d;
    };
    auto separator = [](const wchar_t* key) {
        DockItem d;
        d.kind = ItemKind::Separator;
        d.key = key;
        return d;
    };

    std::vector<const PinnedEntry*> stacks;
    for (auto& p : pinned_) {
        if (p.kind == PinKind::Stack) { stacks.push_back(&p); continue; }
        if (p.kind == PinKind::AppsButton) {
            DockItem d;
            d.kind = ItemKind::AppsButton;
            d.key = L"apps";
            d.name = p.name;
            d.pinned = true;
            out.push_back(std::move(d));
            continue;
        }
        DockItem d = appItem(p.appId, p.name, p.launch);
        d.pinned = true;
        out.push_back(std::move(d));
    }

    // Apps ouvertes non épinglées, dans l'ordre d'ouverture.
    std::vector<const App*> running;
    for (auto& [id, app] : apps_)
        if (!isPinned(id)) running.push_back(&app);
    std::sort(running.begin(), running.end(), [](auto* a, auto* b) { return a->openSeq < b->openSeq; });
    auto runningItems = [&] {
        std::vector<DockItem> v;
        for (auto* a : running) v.push_back(appItem(a->identity.appId, a->identity.displayName, a->identity.launch));
        return v;
    };

    if (!showRecents_) {
        for (auto& d : runningItems()) out.push_back(std::move(d));
        out.push_back(separator(L"sep:1"));
    } else {
        out.push_back(separator(L"sep:1"));
        auto section = runningItems();
        for (auto& r : recents_) {
            if (apps_.contains(r.appId) || isPinned(r.appId)) continue;
            DockItem d = appItem(r.appId, r.displayName, r.launch);
            d.recent = true;
            section.push_back(std::move(d));
        }
        if (!section.empty()) {
            for (auto& d : section) out.push_back(std::move(d));
            out.push_back(separator(L"sep:2"));
        }
    }

    for (auto* s : stacks) {
        DockItem d;
        d.kind = ItemKind::Stack;
        d.key = L"stack:" + s->launch;
        d.name = s->name;
        d.launch = s->launch;
        d.pinned = true;
        out.push_back(std::move(d));
    }

    std::vector<std::pair<std::uint64_t, WindowId>> minimized;
    for (auto& [id, w] : windows_)
        if (w.minimized && !isHidden(w.appId)) minimized.emplace_back(w.minimizedSeq, id);
    std::sort(minimized.begin(), minimized.end());
    for (auto& [seq, id] : minimized) {
        auto& w = windows_.at(id);
        DockItem d;
        d.kind = ItemKind::MinimizedWindow;
        d.key = L"win:" + std::to_wstring(id);
        d.appId = w.appId;
        d.window = id;
        d.name = w.title;
        if (d.name.empty())
            if (auto a = apps_.find(w.appId); a != apps_.end()) d.name = a->second.identity.displayName;
        out.push_back(std::move(d));
    }

    DockItem trash;
    trash.kind = ItemKind::Trash;
    trash.key = L"trash";
    trash.trashFull = trashFull_;
    out.push_back(std::move(trash));
    return out;
}

} // namespace md
