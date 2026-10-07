// Écran Apps, façon Apps de macOS Tahoe : vue plein écran en verre, champ de recherche, grille paginée.
// API modale, comme StackWindow : Échap, un clic dans le vide, un clic droit ou la perte du focus la ferment.
#pragma once
#include <windows.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../core/bgra_image.h"
#include "../popup/menu_window.h"
#include "app_catalog.h"
#include "apps_icon_cache.h"

namespace md {

struct AppsIconStyle {   // réglages des icônes du Dock, pour la même allure
    bool strict = true, dark = false;
    double shapeRatio = 824.0 / 1024.0, cornerRatio = 185.4 / 824.0, jailInset = 0.16, shadowOpacity = 0.5;
    std::wstring customDir;
};

class AppsWindow {
public:
    struct Request {
        std::vector<AppEntry> apps;   // catalogue (catalogFrom)
        HMONITOR monitor = nullptr;   // écran du Dock
        AppsIconStyle icons;
        std::shared_ptr<AppsIconCache> cache;   // facultatif : icônes gardées pour la prochaine ouverture
    };
    // Nom d'analyse de l'app choisie ; chaîne vide : fermée sans choix ; nullopt : la vue n'a pas pu s'ouvrir.
    static std::optional<std::wstring> track(const MenuWindow::Env& env, const Request& request);
};

// Même dessin hors écran (Direct2D sur une bitmap, aucune fenêtre) : fond = fond d'écran Tahoe flouté.
// icons faux : cases de couleur à la place des icônes (tests).
BgraImage appsSnapshot(const std::vector<AppEntry>& apps, const std::wstring& query, int page, bool dark, int width,
                       int height, bool icons, const AppsIconStyle& style = {});

} // namespace md
