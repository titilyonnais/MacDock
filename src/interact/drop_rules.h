// Règles du dépôt de fichiers sur le Dock (logique pure).
#pragma once
#include <string>
#include <vector>

#include "../model/app_model.h"

namespace md {

enum class DropAction { None, Pin, OpenWith, Recycle, MoveInto };

struct DropTargetInfo {
    ItemKind kind = ItemKind::App;   // élément sous le curseur
    bool betweenPinned = false;      // entre deux icônes de la section épinglée (pas sur une icône)
};

DropAction dropAction(const DropTargetInfo& target, const std::vector<std::wstring>& paths);
bool isPinnableFile(const std::wstring& path);   // .exe .lnk .appref-ms, insensible à la casse

} // namespace md
