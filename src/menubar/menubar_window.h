// Barre de menus : une barre d'application en haut de chaque écran (l'active pleine, les autres atténuées), app
// active, menus en verre, icônes d'état et des apps, horloge, couleur du texte selon le fond, plein écran et masquage.
#pragma once
#include <windows.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../app/visibility.h"
#include "../config/metrics.h"
#include "../hud/hud_logic.h"
#include "../hud/hud_window.h"
#include "../ipc/pipe_server.h"
#include "../icons/icon_provider.h"
#include "../model/app_model.h"
#include "../popup/menu_window.h"
#include "../tracker/window_tracker.h"
#include "app_menus.h"
#include "backdrop_sampler.h"
#include "bar_actions.h"
#include "bar_layout.h"
#include "bar_renderer.h"
#include "bar_screens.h"
#include "menubar_settings.h"
#include "status_audio.h"
#include "status_hub.h"
#include "status_menus.h"
#include "traffic_window.h"
#include "window_look.h"
#include "tray_model.h"
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

    // Une barre par écran : sa fenêtre, son rendu, sa mise en page, la couleur de son texte et son masquage.
    struct Screen {
        HWND hwnd = nullptr;
        HMONITOR monitor = nullptr;
        RECT rect{};             // l'écran
        UINT dpi = 96;
        bool primary = false;
        float scale = 1;
        int heightPx = 24;
        int yOffsetPx = 0;       // décalage vertical (masquage) : 0 = visible
        bool appBar = false, visible = true;
        BarRenderer renderer;
        std::wstring font;
        BarLayoutInput layoutIn;
        BarLayout layout;
        bool darkText = false;
        BackdropSampler sampler;
        double lastSample = -1;
        Visibility visibility;
        bool fullscreen = false, visibilityTimer = false;
        int renderFailures = 0;
        std::size_t trayFirst = 0;   // icônes d'apps affichées : trayLaid_[trayFirst…] (les autres n'ont pas la place)
    };

    static LRESULT CALLBACK controlProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK screenProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);                  // fenêtre de contrôle
    LRESULT handleScreen(Screen& s, UINT msg, WPARAM wp, LPARAM lp);   // fenêtre d'une barre

    void loadSettings(bool initial);
    void checkSettingsFile();
    void applySettings();
    void loadLogo();
    void rebuildScreens();   // écrans branchés ou débranchés (différé pendant un menu)
    bool createScreen(Screen& s);
    void destroyScreen(Screen& s);
    Screen* screenOf(HWND hwnd);
    void updateActiveScreen();
    void reposition(Screen& s);
    void repositionAll();
    void registerAppBar(Screen& s);
    void removeAppBar(Screen& s);
    void syncAppBar(Screen& s);
    void onForeground(HWND h);
    void decideDesktopFocus();   // bureau au premier plan sans clic : app suivante, app gardée ou Explorateur
    static void readRealMenus(HWND top, HWND root, Active& a);
    void refreshRealMenu(int real);
    bool syncRealTitles();   // barre Win32 relue (titres) ; true si elle a changé
    void requestUiaTitles(HWND window);
    bool knownWithoutMenuBar(HWND window);   // lu récemment, sans barre de menus : pas de nouvelle requête
    void onUiaTitles(LPARAM result);         // titres lus sur le fil UI Automation, reçus par WM_APP_UIA_TITLES
    void loadRecent();
    void saveRecent();
    std::vector<RecentEntry> withIcons(std::vector<RecentEntry> list, bool documents, float scale) const;
    void relayout();               // contenu commun, puis mise en page de chaque barre
    void layoutScreen(Screen& s);
    void render();                 // toutes les barres
    void render(Screen& s);
    void recoverDevice(Screen& s);   // device perdu : recréé ; sinon sortie en erreur (le lanceur relance la barre)
    void restoreTargetFocus();   // menu fermé sans choix : le clavier retourne à l'app
    void afterMenu();            // menu fermé : capsule ôtée, mises en page et écrans en attente appliqués
    BarFrame frame(const Screen& s) const;
    void onPress(Screen& s, POINT client, bool doubleClick = false);
    void onRightClick(Screen& s, POINT client);
    void openMenu(Screen& s, std::size_t index);   // titre (gauche) ou icône d'état (leftVisible + j)
    MenuWindow::BarLink barLink(const Screen& s, int current) const;   // titres puis icônes d'état (sans menu : vides)
    int trackStatus(Screen& s, std::size_t j, const MenuWindow::BarLink& link, StatusCommand& chosen);
    void runStatus(const StatusCommand& c);
    StatusState statusState();
    void onStatus(LPARAM snapshot);
    void updateStatusItems();   // icônes redessinées si le relevé ou le son ont changé
    void onTray(WPARAM wp, LPARAM lp);   // message du mod (lp : ipc::Message*), ou wp = 1 : connexion ou départ du mod
    void trayClickAt(Screen& s, std::size_t k, int button);
    void openSettingsFile();
    void execute(const MenuAction& a);
    BarContext context(bool recentDocs = false, float scale = 1) const;   // recentDocs : lit le dossier Récents
    MenuWindow::Env menuEnv(const Screen& s) const;
    std::vector<HWND> appWindows() const;
    void scheduleClock();
    void updateClock();
    void startSample(Screen& s);
    void startSamples();
    void stopSamples();   // avant un menu : il capture l'écran à son tour (la pastille du HUD aussi)
    void abortSamples();  // capture d'écran du Dock : échantillons arrêtés, couleur du texte inchangée
    void onScreenshotReveal(bool on);   // ⊞⇧3, ⊞⇧4 : barre et pastilles visibles aux captures, le temps de la copie
    void registerVolumeKeys();          // touches de volume reprises selon settings_.hud (échec journalisé)
    void onVolumeKey(int id);           // volume +, −, sourdine : réglage puis pastille
    void showHud(const HudContent& c);  // pastille sur l'écran du curseur, ravivée
    void stepHud();                     // minuterie du fondu
    void hideHud();
    HudContent volumeContent();         // volume et sortie actuels
    void onSample(Screen& s);
    void finishSample(Screen& s, std::optional<double> luminance);
    bool detectFullscreen(const Screen& s) const;
    void checkFullscreen();
    std::size_t trayShown(const Screen& s) const;   // icônes d'apps de cette barre (cases 0… de la droite)
    void stepVisibility(Screen& s);
    void stepVisibilityAll();
    int runSnapshot(const Options& options);
    static bool systemDarkMode();

    HINSTANCE instance_ = nullptr;
    HWND ctl_ = nullptr;   // fenêtre de contrôle cachée : minuteries, messages des fils, suivi des fenêtres, WM_CLOSE
    bool trace_ = false;
    UINT taskbarCreated_ = 0;   // « TaskbarCreated » : l'Explorateur a redémarré et oublié les zones réservées
    UINT shotRevealMsg_ = 0;   // « MacDockScreenshotReveal » (wParam 1 : visibles aux captures, 0 : exclues de nouveau)
    bool shotReveal_ = false;  // capture d'écran du Dock en cours : pas d'échantillon (il exclurait la barre)
    std::wstring dataDir_;
    MenuBarSettings settings_;
    FILETIME settingsTime_{};
    Metrics glassMetrics_;   // mesures du verre des menus (dock-metrics.json, en lecture seule)
    LogoImage logo_;         // menubar-logo.png, donné au rendu de chaque barre

    std::vector<std::unique_ptr<Screen>> screens_;   // l'écran principal d'abord
    std::size_t activeScreen_ = 0;                   // barre pleine ; les autres sont atténuées
    RebuildGate screensGate_;                        // écrans changés pendant un menu ou pendant leur reconstruction

    TrafficWindow lights_;   // feux tricolores de la fenêtre active
    HWND lastForeground_ = nullptr;   // dernier premier plan traité (rattrapage périodique)
    HWND desktopPrevious_ = nullptr;  // bureau au premier plan sans clic : la fenêtre active juste avant (décision différée)
    bool desktopDecided_ = false;     // le bureau peut s'afficher comme Explorateur (clic dessus, ou décision prise)
    WindowStyler styler_;    // apparence macOS des fenêtres des autres apps (attributs DWM)
    WindowTracker tracker_;
    AppModel model_;
    SystemActions sys_;
    UiaWorker uia_;
    std::atomic<unsigned> uiaLatest_{0};   // dernière demande de titres : les plus anciennes sont abandonnées
    HWND uiaWindow_ = nullptr;             // fenêtre de la dernière demande
    std::map<HWND, ULONGLONG> noMenuBar_;  // fenêtres sans barre de menus accessible (instant de la lecture)
    RecentState recent_;                   // menubar-recent.json
    bool recentDirty_ = false;             // à écrire (minuterie : pas d'écriture à chaque changement d'app)
    mutable IconProvider icons_;           // icônes des Éléments récents
    AudioStatus audio_;                    // son (Core Audio, fil de la barre)
    StatusHub hub_;                        // réseau, radios, lecture en cours, luminosité, batterie (fil de travail)
    StatusSnapshot snap_;                  // dernier relevé du hub
    std::vector<StatusItem> status_;       // icônes système et horloge (cases trayLaid_.size() + j de la droite)
    struct TrayShown {                     // icône d'app placée dans la barre
        ipc::TrayIconEvent e;              // sans ses pixels (dans image)
        std::shared_ptr<const std::vector<std::uint8_t>> image;
    };
    ipc::PipeServer trayPipe_;             // \\.\pipe\MacMenuBar : le mod Windhawk relaie la zone de notification
    TrayModel tray_;
    std::vector<TrayShown> trayLaid_;      // icônes d'apps de la mise en page courante, de gauche à droite

    Active active_;
    BarTarget target_;
    BarMenus menus_;
    std::wstring clock_;
    Screen* menuScreen_ = nullptr;   // barre du menu ouvert (sa capsule)
    int highlight_ = -1;
    std::vector<HWND> hidden_;   // fenêtres masquées par « Masquer… »

    HudWindow hud_;                        // pastille du volume et de la luminosité
    HudFade hudFade_;
    BrightnessGate brightnessGate_;
    HPOWERNOTIFY brightnessNotify_ = nullptr, displayNotify_ = nullptr, powerNotify_ = nullptr;
    bool volumeKeys_ = false;              // touches de volume reprises (sinon : avis Core Audio)
    std::wstring outputName_;              // nom de la sortie par défaut (vide : à relire)
    bool hudStoppedSample_ = false;        // un relevé du fond a été arrêté pour la pastille : à refaire

    bool menuOpen_ = false;
    bool menuSession_ = false;     // openMenu ou menu de la barre en cours (menuOpen_ retombe entre deux titres)
    bool layoutPending_ = false;   // relayout demandé pendant un menu : fait à sa fermeture (menus_ reste stable)
    int exitCode_ = 0;
    static MenuBarApp* self_;
};

} // namespace md
