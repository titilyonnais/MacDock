#include "tray_model.h"

#include <shellapi.h>

#include <algorithm>
#include <cstring>

namespace md {
namespace {

bool hasGuid(const ipc::TrayIconEvent& e) {
    return std::any_of(std::begin(e.guid), std::end(e.guid), [](std::uint8_t b) { return b != 0; });
}

bool sameIcon(const ipc::TrayIconEvent& a, const ipc::TrayIconEvent& b) {
    if (hasGuid(a) || hasGuid(b)) return std::memcmp(a.guid, b.guid, 16) == 0;
    return a.hwnd == b.hwnd && a.uid == b.uid;
}

} // namespace

void TrayModel::update(const ipc::TrayIconEvent& e) {
    for (auto& i : icons_)
        if (sameIcon(i.e, e)) {
            i.e = e;
            return;
        }
    icons_.push_back({e, ++next_});
}

void TrayModel::remove(const ipc::TrayIconEvent& e) {
    std::erase_if(icons_, [&](const TrayIcon& i) { return sameIcon(i.e, e); });
}

void TrayModel::clear() { icons_.clear(); }

bool TrayModel::prune(const std::function<bool(std::uint64_t)>& alive) {
    return std::erase_if(icons_, [&](const TrayIcon& i) { return !alive(i.e.hwnd); }) != 0;
}

std::vector<const TrayIcon*> TrayModel::visible() const {
    std::vector<const TrayIcon*> out;
    for (const auto& i : icons_)
        if (!i.e.hidden && i.e.w && i.e.h) out.push_back(&i);
    std::sort(out.begin(), out.end(), [](const TrayIcon* a, const TrayIcon* b) { return a->order > b->order; });
    return out;
}

const TrayIcon* TrayModel::find(const ipc::TrayIconEvent& key) const {
    for (const auto& i : icons_)
        if (sameIcon(i.e, key)) return &i;
    return nullptr;
}

std::vector<TrayPost> trayClick(const ipc::TrayIconEvent& e, int button, POINT pt) {
    std::vector<TrayPost> out;
    if (!(e.flags & NIF_MESSAGE) || e.callback == 0) return out;
    const UINT down = button == 0 ? WM_LBUTTONDOWN : button == 2 ? WM_LBUTTONDBLCLK : WM_RBUTTONDOWN;
    const UINT up = button == 1 ? WM_RBUTTONUP : WM_LBUTTONUP;
    if (button == 2) {   // double-clic gauche : comme l'Explorateur, le message de double-clic puis le relâchement
        if (e.version >= NOTIFYICON_VERSION_4) {
            const WPARAM wp = MAKEWPARAM(LOWORD(pt.x), LOWORD(pt.y));
            out.push_back({wp, LPARAM(MAKELPARAM(down, e.uid)), e.callback});
            out.push_back({wp, LPARAM(MAKELPARAM(up, e.uid)), e.callback});
        } else {
            out.push_back({WPARAM(e.uid), LPARAM(down), e.callback});
            out.push_back({WPARAM(e.uid), LPARAM(up), e.callback});
        }
        return out;
    }
    if (e.version >= NOTIFYICON_VERSION_4) {
        // Version 4 : point d'ancrage dans wParam, message et identifiant dans lParam, puis l'action de haut niveau.
        const WPARAM wp = MAKEWPARAM(LOWORD(pt.x), LOWORD(pt.y));
        const auto lp = [&](UINT msg) { return LPARAM(MAKELPARAM(msg, e.uid)); };
        out.push_back({wp, lp(down), e.callback});
        out.push_back({wp, lp(up), e.callback});
        out.push_back({wp, lp(button == 0 ? NIN_SELECT : WM_CONTEXTMENU), e.callback});
    } else {
        out.push_back({WPARAM(e.uid), LPARAM(down), e.callback});
        out.push_back({WPARAM(e.uid), LPARAM(up), e.callback});
    }
    return out;
}

} // namespace md
