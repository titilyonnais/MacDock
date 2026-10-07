// Fenêtre de la barre de menus : barre d'application en haut de l'écran principal, app active, menus en verre,
// horloge, couleur du texte selon le fond, plein écran et masquage automatique.
#pragma once
#include <windows.h>

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "../app/visibility.h"
#include "../config/metrics.h"
#include "../icons/icon_provider.h"
#include "../model/app_model.h"
#include "../popup/menu_window.h"
#include "../tracker/window_tracker.h"
#include "app_menus.h"
#include "backdrop_sampler.h"
#include "bar_actions.h"
#include "bar_layout.h"
#include "bar_renderer.h"
#include "menubar_settings.h"
#include "uia_menu.h"

namespace md {

class MenuBarApp {
public:
    struct Options {
        bool trace = false;
        std::wstring snapshot;    // PNG : rendu hors écran de la barre puis sortie
        std::wstring wallpaper;   // PNG de fond pour --snapshot (sinon dégradé selon le thème)
        std::wstring app;         // app affichée dans --snapshot
        std::optional<bool> dark; // thème forcé (--theme light|dark)
        int open = -1;            // --snapshot : titre dont la capsule est dessinée (menu ouvert)
    };
    int run(HINSTANCE instance, const Options& options);

private:
    struct Active {               // app affichée dans la barre
        std::wstring name, appId, exePath, launch;
        bool explorer = false, desktop = false;
        HWND menuOwner = nullptr;   // fenêtre dont la barre montre les vrais menus
        MenuSource source = MenuSource::Generic;
        std::vector<RawMenuItem> real;
    };

    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void loadSettings(bool initial);
    void checkSettingsFile();
    void applySettings();
    void loadLogo();
    void reposition();
    void registerAppBar();
    void removeAppBar();
    void syncAppBar();
    void onForeground(HWND h);
    static void readRealMenus(HWND top, HWND root, Active& a);
    void refreshRealMenu(int real);
    bool syncRealTitles();   // barre Win32 relue (titres) ; true si elle a changé
    void requestUiaTitles(HWND window);
    bool knownWithoutMenuBar(HWND window);   // lu récemment, sans barre de menus : pas de nouvelle requête   // titres lus sur le fil UI Automation, reçus par WM_APP_UIA_TITLES
    void onUiaTitles(LPARAM result);
    void loadRecent();
    void saveRecent();
    std::vector<RecentEntry> withIcons(std::vector<RecentEntry> list, bool documents) const;   // à l'ouverture d'un vrai menu : l'app le prépare, la barre le relit
    void relayout();
    void render();
    void recoverDevice();   // device perdu : recréé ; sinon sortie en erreur (le lanceur relance la barre)
    void restoreTargetFocus();   // menu fermé sans choix : le clavier retourne à l'app
    BarFrame frame() const;
    void onPress(POINT client);
    void onRightClick(POINT client);
    void openMenu(std::size_t index);
    void execute(const MenuAction& a);
    BarContext context(bool recentDocs = false) const;   // recentDocs : lit le dossier Récents (ouverture d'un menu)
    MenuWindow::Env menuEnv() const;
    std::vector<HWND> appWindows() const;
    void scheduleClock();
    void updateClock();
    void startSample();
    void onSample();
    void finishSample(std::optional<double> luminance);
    bool detectFullscreen() const;
    void checkFullscreen();
    void stepVisibility();
    int runSnapshot(const Options& options);
    static bool systemDarkMode();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool trace_ = false;
    std::wstring dataDir_;
    MenuBarSettings settings_;
    FILETIME settingsTime_{};
    Metrics glassMetrics_;   // mesures du verre des menus (dock-metrics.json, en lecture seule)

    WindowTracker tracker_;
    AppModel model_;
    BarRenderer renderer_;
    std::wstring font_;
    SystemActions sys_;
    UiaWorker uia_;
    std::atomic<unsigned> uiaLatest_{0};   // dernière demande de titres : les plus anciennes sont abandonnées
    HWND uiaWindow_ = nullptr;             // fenêtre de la dernière demande
    std::map<HWND, ULONGLONG> noMenuBar_;  // fenêtres sans barre de menus accessible (instant de la lecture)
    RecentState recent_;                   // menubar-recent.json
    bool recentDirty_ = false;             // à écrire (minuterie : pas d'écriture à chaque changement d'app)
    mutable IconProvider icons_;           // icônes des Éléments récents

    RECT monitor_{};
    float scale_ = 1;
    int heightPx_ = 24;
    int yOffsetPx_ = 0;      // décalage vertical (masquage) : 0 = visible
    bool appBar_ = false;
    bool visible_ = true;

    Active active_;
    BarTarget target_;
    BarMenus menus_;
    BarLayoutInput layoutIn_;
    BarLayout layout_;
    std::wstring clock_;
    int highlight_ = -1;
    std::vector<HWND> hidden_;   // fenêtres masquées par « Masquer… »

    bool darkText_ = false;
    BackdropSampler sampler_;
    double lastSample_ = -1;

    Visibility visibility_;
    bool fullscreen_ = false, menuOpen_ = false, visibilityTimer_ = false;
    bool layoutPending_ = false;   // relayout demandé pendant un menu : fait à sa fermeture (menus_ reste stable)
    int renderFailures_ = 0;
    int exitCode_ = 0;
    static MenuBarApp* self_;
};

} // namespace md
