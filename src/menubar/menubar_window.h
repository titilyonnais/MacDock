// Fenêtre de la barre de menus : barre d'application en haut de l'écran principal, app active, menus en verre,
// horloge, couleur du texte selon le fond, plein écran et masquage automatique.
#pragma once
#include <windows.h>

#include <optional>
#include <string>
#include <vector>

#include "../app/visibility.h"
#include "../config/metrics.h"
#include "../model/app_model.h"
#include "../popup/menu_window.h"
#include "../tracker/window_tracker.h"
#include "app_menus.h"
#include "backdrop_sampler.h"
#include "bar_actions.h"
#include "bar_layout.h"
#include "bar_renderer.h"
#include "menubar_settings.h"

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
        std::wstring name, appId, exePath;
        bool explorer = false, desktop = false;
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
    void relayout();
    void render();
    BarFrame frame() const;
    void onPress(POINT client);
    void onRightClick(POINT client);
    void openMenu(std::size_t index);
    void execute(const MenuAction& a);
    BarContext context() const;
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
    static MenuBarApp* self_;
};

} // namespace md
