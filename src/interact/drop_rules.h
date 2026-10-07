// Règles du dépôt de fichiers sur le Dock (logique pure).
#pragma once
#include <windows.h>
#include <ole2.h>

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

// Effets OLE. chooseEffect : effet proposé selon ce que la source permet (DROPEFFECT_NONE si l'action
// n'est pas possible sans risque). Corbeille et pile exigent MOVE ; ouvrir ou épingler n'acceptent jamais MOVE.
DWORD chooseEffect(DropAction action, DWORD allowed);
// Effet rendu par Drop : le Dock fait lui-même les déplacements (plus tard), la source ne doit rien supprimer.
DWORD dropReturnEffect(DropAction action, DWORD chosen);
bool dockPerformsMove(DropAction action);

} // namespace md
