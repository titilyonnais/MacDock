#include "drop_rules.h"

#include "../core/strings.h"

namespace md {

bool isPinnableFile(const std::wstring& path) {
    const std::wstring p = toLower(path);
    for (const wchar_t* ext : {L".exe", L".lnk", L".appref-ms"}) {
        const std::wstring e = ext;
        if (p.size() > e.size() && p.ends_with(e)) return true;
    }
    return false;
}

DropAction dropAction(const DropTargetInfo& target, const std::vector<std::wstring>& paths) {
    if (paths.empty()) return DropAction::None;
    if (target.betweenPinned) return paths.size() == 1 && isPinnableFile(paths[0]) ? DropAction::Pin : DropAction::None;
    switch (target.kind) {
        case ItemKind::App: return DropAction::OpenWith;
        case ItemKind::Trash: return DropAction::Recycle;
        case ItemKind::Stack: return DropAction::MoveInto;
        default: return DropAction::None;
    }
}

bool dockPerformsMove(DropAction action) {
    return action == DropAction::Recycle || action == DropAction::MoveInto;
}

DWORD chooseEffect(DropAction action, DWORD allowed) {
    switch (action) {
        case DropAction::Recycle:
        case DropAction::MoveInto: return (allowed & DROPEFFECT_MOVE) ? DROPEFFECT_MOVE : DROPEFFECT_NONE;
        case DropAction::Pin:
            if (allowed & DROPEFFECT_LINK) return DROPEFFECT_LINK;
            return (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_NONE;
        case DropAction::OpenWith:
            if (allowed & DROPEFFECT_COPY) return DROPEFFECT_COPY;
            return (allowed & DROPEFFECT_LINK) ? DROPEFFECT_LINK : DROPEFFECT_NONE;
        default: return DROPEFFECT_NONE;
    }
}

DWORD dropReturnEffect(DropAction action, DWORD chosen) {
    return dockPerformsMove(action) ? DROPEFFECT_NONE : chosen;
}

} // namespace md
