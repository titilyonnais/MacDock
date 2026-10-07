#include "apps_folder.h"

#include <knownfolders.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <chrono>

#include "../core/log.h"

using Microsoft::WRL::ComPtr;

namespace md {

namespace {
std::wstring displayName(IShellItem* item, SIGDN kind) {
    LPWSTR p = nullptr;
    std::wstring out;
    if (SUCCEEDED(item->GetDisplayName(kind, &p)) && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}
} // namespace

std::vector<AppEntry> readAppsFolder() {
    std::vector<AppEntry> out;
    ComPtr<IShellItem> folder;
    ComPtr<IEnumShellItems> items;
    if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))) ||
        FAILED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items))))
        return out;
    for (;;) {
        ComPtr<IShellItem> item;
        ULONG fetched = 0;
        if (items->Next(1, &item, &fetched) != S_OK || !fetched) break;
        out.push_back({displayName(item.Get(), SIGDN_NORMALDISPLAY), displayName(item.Get(), SIGDN_PARENTRELATIVEPARSING)});
    }
    return out;
}

AppCatalog::~AppCatalog() {
    if (thread_.joinable()) thread_.join();
}

void AppCatalog::refreshAsync() {
    std::lock_guard lock(mutex_);
    if (running_) return;
    if (thread_.joinable()) thread_.join();   // la précédente a fini (running_ faux)
    running_ = true;
    thread_ = std::thread([this] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        std::vector<AppEntry> list = catalogFrom(readAppsFolder());
        if (SUCCEEDED(com)) CoUninitialize();
        {
            std::lock_guard lock(mutex_);
            if (!list.empty() || !loaded_) apps_ = std::move(list);   // une lecture ratée garde la précédente
            loaded_ = true;
            running_ = false;
        }
        cv_.notify_all();
    });
}

std::vector<AppEntry> AppCatalog::get(DWORD waitMs) {
    std::unique_lock lock(mutex_);
    if (!cv_.wait_for(lock, std::chrono::milliseconds(waitMs), [this] { return loaded_; }))
        log::warn(L"Apps : le catalogue n'est pas encore lu après %lu ms", waitMs);
    return apps_;
}

} // namespace md
