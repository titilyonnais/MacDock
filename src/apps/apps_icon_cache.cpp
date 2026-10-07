#include "apps_icon_cache.h"

namespace md {

void AppsIconCache::setStyle(const std::wstring& signature) {
    std::lock_guard lock(mutex_);
    if (signature == style_) return;
    style_ = signature;
    images_.clear();
}

IconProvider::ImagePtr AppsIconCache::find(const std::wstring& key, int px) {
    std::lock_guard lock(mutex_);
    auto it = images_.find({key, px});
    return it == images_.end() ? nullptr : it->second;
}

void AppsIconCache::put(const std::wstring& key, int px, IconProvider::ImagePtr image) {
    if (!image) return;
    std::lock_guard lock(mutex_);
    images_[{key, px}] = std::move(image);
}

} // namespace md
