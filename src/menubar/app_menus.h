// Menus de la barre (logique pure) : menu du système (logo), menu de l'app (en gras), menus génériques ou de
// l'Explorateur. Chaque entrée porte une action typée, exécutée par bar_actions.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../popup/menu_model.h"

namespace md {

enum class ActionKind {
    None,
    Shortcut,         // arg : raccourci (« Ctrl+S ») envoyé à l'app
    CloseWindow,      // WM_CLOSE à la fenêtre active de l'app
    Minimize, Zoom,   // fenêtre active : réduire ; agrandir ou restaurer
    BringAllToFront,  // toutes les fenêtres de l'app au premier plan
    ActivateWindow,   // window : fenêtre de la liste
    HideApp, HideOthers, ShowAll, QuitApp, AboutApp,
    EmptyTrash,       // Explorateur : vider la Corbeille (confirmation de l'Explorateur)
    OpenUri,          // arg : ms-settings:…, exécutable, dossier
    GoTo,             // arg : dossier (shell:…) ; dans la fenêtre de l'Explorateur active, sinon une nouvelle
    Sleep, Lock, SignOut, Restart, Shutdown,
};

struct MenuAction {
    ActionKind kind = ActionKind::None;
    std::wstring arg;
    std::uint64_t window = 0;
};

struct BarMenu {
    std::wstring title;   // vide pour le logo
    MenuModel model;
    bool bold = false, logo = false;
};

struct BarContext {
    std::wstring appName, userName;
    bool explorer = false;   // l'Explorateur ou le bureau a le premier plan
    bool desktop = false;    // le bureau lui-même : pas de fenêtre à fermer ni d'historique
    std::vector<std::pair<std::uint64_t, std::wstring>> windows;   // fenêtres de l'app (id, titre)
    std::uint64_t activeWindow = 0;
};

struct BarMenus {
    std::vector<BarMenu> menus;
    std::map<int, MenuAction> actions;   // identifiant d'entrée → action
};

BarMenus buildBarMenus(const BarContext& c);

} // namespace md
