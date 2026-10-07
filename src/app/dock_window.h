// Fenêtre du Dock : orchestration des modules, messages système, boucle d'animation.
#pragma once
#include <windows.h>

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <thread>

#include "../apps/apps_folder.h"
#include "../apps/apps_icon_cache.h"
#include "../apps/apps_window.h"
#include "../config/metrics.h"
#include "../config/settings.h"
#include "../glass/backdrop_capture.h"
#include "../icons/icon_provider.h"
#include "../interact/hot_corners.h"
#include "../ipc/pipe_server.h"
#include "../model/app_model.h"
#include "../popup/menu_window.h"
#include "../popup/stack_window.h"
#include "../render/dock_renderer.h"
#include "../render/sprite_renderer.h"
#include "../switcher/switcher_logic.h"
#include "../switcher/switcher_window.h"
#include "../theme/theme_system.h"
#include "../tracker/window_tracker.h"
#include "dock_controller.h"
#include "genie_window.h"
#include "min_animate.h"
#include "monitor_choice.h"
#include "thumbnails.h"
#include "visibility.h"
#include "sprite_window.h"

namespace md {

class DockApp {
public:
    struct Options {
        bool trace = false;
        std::wstring snapshot;           // chemin PNG : rendu hors écran puis sortie
        std::optional<double> hover;     // position simulée du curseur (points depuis le centre)
        std::wstring wallpaper;          // PNG de fond pour la capture (redimensionné à la fenêtre)
        std::wstring reference;          // PNG de référence (capture de macOS) à comparer
        std::wstring diff;               // carte de différence (PNG) ; diff.txt écrit à côté
        std::optional<bool> dark;        // thème forcé (--theme light|dark) ; sinon celui du système
    };
    int run(HINSTANCE instance, const Options& options);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK mouseHookProc(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK keyboardHookProc(int code, WPARAM wp, LPARAM lp);   // Alt+Tab du sélecteur
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void loadConfig(bool initial);
    void applySettings();
    void savePinned();
    void saveSettings();
    void reposition();
    HMONITOR dockMonitor();              // écran choisi (enregistré, sinon principal), liste des écrans rafraîchie
    void onDisplayChanged();             // écran, résolution ou DPI changés : icônes, place, carte, capture
    void checkScreenPush(POINT screen);  // curseur poussé contre le bord du Dock sur un autre écran
    void registerAppBar();
    void removeAppBar();
    void onMouse(POINT screen);
    void setTransparent(bool transparent);
    void onClick(std::size_t index);
    void activateItem(const DockItem& item);
    void showContextMenu(std::optional<std::size_t> index);   // nullopt : menu du Dock
    void openApps();                                           // écran Apps (repli : menu Démarrer)
    void openSpotlight();                                      // Spotlight ; ferme celui qui est ouvert
    void registerSpotlightHotkey();                            // raccourci du réglage spotlightHotkey
    void openMissionControl();                                 // Mission Control ; ferme celui qui est ouvert
    void registerMissionHotkey();                              // raccourci du réglage missionControlHotkey
    void registerSwitcherHotkey();                             // Alt+Tab et Alt+Maj+Tab (réglage appSwitcherHotkey)
    void switcherKey(int id);                                  // raccourcis du sélecteur et de sa session
    void switcherTick();                                       // minuterie de la session : Alt relâché, panneau
    void endSwitch(bool activate);                             // fin de session ; active l'app choisie
    void checkHotCorner(POINT screen);                         // coins actifs : pointeur poussé dans un coin
    void runHotCorner(HotCornerAction action);
    bool fullscreenAt(POINT screen) const;   // plein écran sur l'écran du point (jeu, vidéo, présentation)
    bool fullscreenOn(HWND fg, HMONITOR mon, const RECT& monitorRc) const;
    AppsIconStyle appsIconStyle() const;                       // icônes des apps comme celles du Dock
    void openStack(std::size_t index);                        // pile ouverte en éventail, en grille ou en liste
    std::size_t listCapacity(const StackWindow::Request& r) const;
    MenuWindow::Env popupEnv();                               // environnement des menus et des piles
    void renderNow();
    void requestFrame();
    void startMouseThread();
    void startConfigWatcher();
    int runSnapshot(const Options& options);
    void onHotKey(int id);
    void updateGlass();       // applique settings_.glass : exclusion de la capture, démarrage ou arrêt
    void restartCapture();
    void pauseCapture();      // le temps d'un menu (une seule duplication de l'écran par processus)
    void resumeCapture();
    void onBackdrop();
    void watchTrash();
    void watchStacks();         // surveille les dossiers des piles épinglées (réenregistre si la liste change)
    void refreshStacks();       // aperçu de chaque pile (icône « Pile ») selon son tri
    void registerDropTarget();
    void performDrop();
    void syncAppBar();                 // zone réservée seulement sans masquage automatique
    bool detectFullscreen() const;
    void checkFullscreen();
    bool stepVisibility(double now);
    void refreshTrash();        // WM_APP_BACKDROP : nouvelle image d'arrière-plan ou changement d'état
    bool initRenderer();
    void onPointerUp(POINT client);
    void logItemPositions(const RenderFrame& frame);
    void updateDragSprite();
    bool stepPoof(double now);   // true tant que le nuage s'anime
    // Effet génie : réduction (de la fenêtre vers sa case) ou restauration depuis le Dock (de la case vers la
    // fenêtre, restaurée à la fin). false : pas d'animation possible.
    bool startGenie(HWND window, bool restore);
    bool stepGenie(double now);   // true tant qu'il s'anime
    void restoreFromDock(HWND window);
    bool rendererOnDockAdapter();   // le device de rendu est-il sur la carte qui pilote l'écran du Dock ?
    static bool systemDarkMode();

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    bool trace_ = false;
    bool snapshot_ = false;
    std::wstring dataDir_;
    Settings settings_;
    Metrics metrics_;
    AppModel model_;
    WindowTracker tracker_;
    IconProvider icons_;
    DockRenderer renderer_;
    DockController controller_;
    ipc::PipeServer pipe_;

    RECT monitor_{};
    std::vector<MonitorInfo> monitors_;   // écrans connus (rafraîchis à chaque placement)
    std::wstring screenName_;             // écran où le Dock est placé (\\.\DISPLAYn)
    ScreenPush screenPush_;               // poussée en cours vers un autre écran
    POINT origin_{};
    float scale_ = 1;
    bool dark_ = false;
    bool transparent_ = true;
    bool appBar_ = false;
    std::optional<std::size_t> pressed_;
    UINT taskbarCreated_ = 0;
    UINT spotlightMsg_ = 0;              // « MacDockSpotlight » : loupe de la barre de menus
    UINT missionMsg_ = 0;                // « MacDockMissionControl » : coins actifs
    std::wstring missionHotkeyOn_;       // raccourci enregistré (vide : aucun)
    std::wstring spotlightHotkeyOn_;     // raccourci enregistré (vide : aucun)
    std::wstring switcherHotkeyOn_;      // raccourci enregistré (vide : aucun)
    AppMru mru_;                         // apps de la plus récemment activée à la plus ancienne
    SwitchSession switch_;               // Alt+Tab en cours
    std::vector<std::wstring> switchApps_;   // rangée de la session (appId)
    SwitcherWindow switcher_;
    bool switchPanel_ = false;           // panneau affiché (capture du Dock en pause)
    HotCornerTracker corners_;
    HotCornerAction pendingCorner_ = HotCornerAction::Off;   // action différée (veille de l'écran, économiseur)
    std::vector<RECT> cornerScreens_;    // écrans (relus à chaque changement d'affichage)
    bool running_ = true;
    bool wakeAnimation_ = true;
    std::atomic<bool> wakePosted_{false};
    std::atomic<ULONGLONG> lastUiBeat_{0};
    int renderFailures_ = 0;
    std::shared_ptr<const OverlayImage> overlay_;   // superposition de calibration (Ctrl+Alt+Maj+O)
    float overlayOpacity_ = 0.5f;
    int exitCode_ = 0;

    BackdropCapture capture_;
    bool excluded_ = false;    // fenêtre exclue des captures (WDA_EXCLUDEFROMCAPTURE)
    bool glassLive_ = false;
    bool capturePaused_ = false;   // capture suspendue pendant un menu : la dernière image reste valable   // une image d'arrière-plan a été reçue : verre réel
    int capturesTaken_ = 0;    // compteur [perf]

    SpriteRenderer sprites_;
    SpriteWindow dragSprite_, poofSprite_;
    struct { std::wstring key; bool removing = false; UINT px = 0, w = 0, h = 0; bool dark = false; } dragSpriteKey_;
    double poofStart_ = -1;
    std::uint64_t loggedRevision_ = 0;   // [trace] dernière révision du modèle dont les positions ont été journalisées
    POINT poofCenter_{};
    bool captureFailed_ = false;   // échec définitif : pas de nouvel essai avant un changement d'affichage
    ULONG trashNotify_ = 0;        // SHChangeNotifyRegister sur la Corbeille
    std::vector<ULONG> stackNotify_;            // un par dossier de pile
    std::vector<std::wstring> watchedStacks_;   // dossiers surveillés
    double stacksFirstEvent_ = -1;              // premier avis d'une rafale en cours (-1 : aucune)
    class DropTarget* dropTarget_ = nullptr;
    struct PendingDrop {
        DropHover hover;
        DockItem item;
        std::vector<std::wstring> paths;
    };
    std::optional<PendingDrop> pendingDrop_;   // exécuté après le retour de Drop (WM_APP_DROP)
    Visibility visibility_;
    Thumbnails thumbnails_;
    GenieWindow genie_;
    MinAnimateGuard minAnimate_{realMinAnimateApi()};
    AppCatalog apps_;
    std::shared_ptr<AppsIconCache> appsIcons_ = std::make_shared<AppsIconCache>();     // apps de l'écran Apps, relues après chaque ouverture
    ThemeJob themeJob_;   // thème macOS appliqué ou rétabli hors du fil de l'interface
    std::map<std::uint64_t, RECT> shownTiles_;   // cases des miniatures de la dernière image (pixels de la fenêtre)
    double genieSettleUntil_ = -1;   // fin d'ouverture : dernière image gardée par-dessus la fenêtre restaurée
    std::map<std::uint64_t, RECT> lastSeen_;     // dernier rectangle à l'écran des fenêtres au premier plan
    void noteForeground();                        // relève le rectangle de la fenêtre au premier plan
    GenieRun genieRun() const;
    bool fullscreen_ = false, cursorAtEdge_ = false, cursorInDock_ = false, menuOpen_ = false;
    bool swallowClick_ = false;   // appui qui a fermé Spotlight : son relâchement ne clique pas
    bool loggedHidden_ = false;
    DockPosition placedPosition_ = DockPosition::Bottom;   // bord où la fenêtre est placée

    std::thread mouseThread_;
    DWORD mouseThreadId_ = 0;
    std::atomic<LONG> mouseX_{0}, mouseY_{0};
    std::atomic<bool> mousePending_{false};
    std::atomic<bool> switchKeysOn_{false};    // le crochet clavier prend Alt+Tab (réglage appSwitcherHotkey)
    std::atomic<bool> switchSession_{false};   // session en cours : Échap, flèches, Q et H aussi
    std::thread configThread_;
    HANDLE stopEvent_ = nullptr;
    static DockApp* self_;
};

} // namespace md
