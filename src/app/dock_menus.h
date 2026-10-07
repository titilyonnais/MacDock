// Contenu des menus contextuels du Dock (spec 4.5), construit à partir d'une copie de l'élément :
// le menu reste valable même si le modèle change pendant qu'il est ouvert.
#pragma once
#include <string>
#include <utility>
#include <vector>

#include "../config/settings.h"
#include "../model/app_model.h"
#include "../popup/menu_model.h"

namespace md {

enum MenuCmd : int {
    kCmdOpen = 1, kCmdKeep, kCmdLogin, kCmdReveal, kCmdShowAll, kCmdHide, kCmdQuit,
    kCmdAutohide, kCmdMagnify, kCmdPosLeft, kCmdPosBottom, kCmdPosRight, kCmdSettings,
    kCmdTrashOpen, kCmdTrashEmpty, kCmdRemove, kCmdQuitDock, kCmdRestore, kCmdCloseWindow,
    kCmdSortDateAdded, kCmdSortName, kCmdSortModified, kCmdSortKind,   // pile : Trier par
    kCmdViewAuto, kCmdViewFan, kCmdViewGrid,                           // pile : Présenter le contenu comme
    kCmdWindowBase = 1000   // + index dans MenuContext::windows
};

struct MenuContext {
    DockItem item;                  // copie de l'élément cliqué (Separator pour le séparateur)
    std::wstring exePath;           // App : exécutable (vide pour une app empaquetée)
    bool openAtLogin = false;       // App : déjà ouverte à la connexion
    bool trashFull = false;
    StackView stackView = StackView::Auto;        // Stack : réglages de la pile épinglée
    StackSort stackSort = StackSort::DateAdded;
    Settings settings;
    std::vector<std::pair<WindowId, std::wstring>> windows;   // App ouverte : fenêtres et titres
};

MenuModel buildDockMenu(const MenuContext& c);
// App empaquetée (Store) : lancée par son AUMID, exe sous WindowsApps (ni « Run », ni Explorateur).
bool isPackagedApp(const std::wstring& exePath, const std::wstring& launch);

} // namespace md
