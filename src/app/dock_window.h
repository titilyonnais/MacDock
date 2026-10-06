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
#include "../icons/icon_provider.h"
#include "../ipc/pipe_server.h"
#include "../model/app_model.h"
#include "../render/dock_renderer.h"
#include "../tracker/window_tracker.h"
#include "dock_controller.h"

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
    };
    int run(HINSTANCE instance, const Options& options);

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK mouseHookProc(int code, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void loadConfig(bool initial);
    void applySettings();
    void savePinned();
    void reposition();
    void registerAppBar();
    void removeAppBar();
    void onMouse(POINT screen);
    void setTransparent(bool transparent);
    void onClick(std::size_t index);
    void showContextMenu(POINT screen, std::optional<std::size_t> index);
    void renderNow();
    void requestFrame();
    void startMouseThread();
    void startConfigWatcher();
    int runSnapshot(const Options& options);
    void onHotKey(int id);
    void updateGlass();       // applique settings_.glass : exclusion de la capture, démarrage ou arrêt
    void restartCapture();
    void onBackdrop();        // WM_APP_BACKDROP : nouvelle image d'arrière-plan ou changement d'état
    bool initRenderer();
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
    bool glassLive_ = false;   // une image d'arrière-plan a été reçue : verre réel
    int capturesTaken_ = 0;    // compteur [perf]

    std::thread mouseThread_;
    DWORD mouseThreadId_ = 0;
    std::atomic<LONG> mouseX_{0}, mouseY_{0};
    std::atomic<bool> mousePending_{false};
    std::thread configThread_;
    HANDLE stopEvent_ = nullptr;
    static DockApp* self_;
};

} // namespace md
