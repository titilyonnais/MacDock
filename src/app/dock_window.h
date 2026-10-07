// Fenêtre du Dock : orchestration des modules, messages système, boucle d'animation.
#pragma once
#include <windows.h>

#include <atomic>
#include <optional>
#include <string>
#include <thread>

#include "../config/metrics.h"
#include "../config/settings.h"
#include "../glass/backdrop_capture.h"
#include "monitor_choice.h"
#include "../icons/icon_provider.h"
#include "../ipc/pipe_server.h"
#include "../model/app_model.h"
#include "../render/dock_renderer.h"
#include "../render/sprite_renderer.h"
#include "../tracker/window_tracker.h"
#include "dock_controller.h"
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
    std::wstring pushTarget_;             // écran visé par une poussée en cours
    POINT origin_{};
    float scale_ = 1;
    bool dark_ = false;
    bool transparent_ = true;
    bool appBar_ = false;
    std::optional<std::size_t> pressed_;
    UINT taskbarCreated_ = 0;
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
    bool captureFailed_ = false;
    ULONG trashNotify_ = 0;
    class DropTarget* dropTarget_ = nullptr;
    struct PendingDrop {
        DropHover hover;
        DockItem item;
        std::vector<std::wstring> paths;
    };
    std::optional<PendingDrop> pendingDrop_;   // exécuté après le retour de Drop (WM_APP_DROP)
    Visibility visibility_;
    Thumbnails thumbnails_;
    bool fullscreen_ = false, cursorAtEdge_ = false, cursorInDock_ = false, menuOpen_ = false;
    bool loggedHidden_ = false;
    DockPosition placedPosition_ = DockPosition::Bottom;   // bord où la fenêtre est placée        // SHChangeNotifyRegister sur la Corbeille   // échec définitif : pas de nouvel essai avant un changement d'affichage

    std::thread mouseThread_;
    DWORD mouseThreadId_ = 0;
    std::atomic<LONG> mouseX_{0}, mouseY_{0};
    std::atomic<bool> mousePending_{false};
    std::thread configThread_;
    HANDLE stopEvent_ = nullptr;
    static DockApp* self_;
};

} // namespace md
