// Feux tricolores à l'écran : un petit calque posé à la place des boutons réduire / agrandir / fermer de la fenêtre
// active, juste au-dessus d'elle dans l'ordre d'affichage, qui la suit quand elle bouge et lui envoie ses commandes
// système. Ces boutons (de Windows, ou dessinés par l'app : Chromium, Electron…) sont repérés par DWM ou en sondant
// la fenêtre (WM_NCHITTEST) ; le calque les recouvre, rien du contenu de l'app n'est caché.
#pragma once
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "traffic_lights.h"

namespace md {

UINT effectiveDpi(HWND h);            // DPI réel de l'écran de la fenêtre
LightsWindowInfo readInfo(HWND h);    // style, cadre, zone client, processus… d'une fenêtre de premier niveau

// Les pastilles vivent sur leur propre fil : la sonde des boutons (jusqu'à 60 ms d'attente d'une app occupée) ne fige
// jamais la barre de menus. Les méthodes publiques postent à ce fil.
class TrafficWindow {
public:
    ~TrafficWindow() { destroy(); }
    bool create(HINSTANCE instance);   // lance le fil et ses calques
    // Fenêtre active (nullptr : aucune). Décide (wantsLights), sinon masque le calque.
    void attach(HWND target, LightsMode mode);
    void detach();
    void destroy();
    HWND target() const { return publicTarget_.load(); }
    // Capture d'écran du Dock (⊞⇧3, ⊞⇧4) : calques visibles aux captures le temps de la copie. Synchrone (le fil
    // répond en moins de 200 ms, sinon on n'attend plus).
    void setCaptureVisible(bool on);

private:
    enum class Spot { None, Over };   // pas de pastilles (aucun bouton trouvé), ou sur les boutons de la fenêtre
    struct Placement {
        HWND target = nullptr;
        SIZE size{};
        bool zoomed = false, valid = false;
        UINT dpi = 0;
        Spot spot = Spot::None;
        RECT buttons{};          // boutons de la fenêtre, relatifs au coin haut droit du cadre
    };

    void run();
    bool ensureLayers();               // crée les calques manquants
    void destroyLayers();
    void doAttach(HWND target, LightsMode mode);
    void doDetach();
    void moveLayer(HWND layer, const RECT& screenRect);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK onEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD);
    LRESULT handle(HWND from, UINT msg, WPARAM wp, LPARAM lp);
    void place(bool resample, bool probe = false);   // relit la cible, décide, dessine si besoin, replace les calques
    Placement measure(const LightsWindowInfo& info, UINT dpi, bool& complete) const;
    void hide();
    void sample(const RECT& frame, UINT dpi);
    void paint();
    void paintLayer(HWND layer, const LightsLayout& layout, SIZE& painted, std::uint32_t patchColor);
    void raise();                // juste au-dessus de la cible
    void unhook();

    HWND hwnd_ = nullptr, target_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    LightsMode mode_ = LightsMode::Standard;
    LightsLayout layout_{};
    LightsState state_{};
    Placement placement_{};
    Spot spot_ = Spot::None;
    double scale_ = 1;
    bool shown_ = false, tracking_ = false, painted_ = false;
    int pressed_ = -1;
    bool dragging_ = false;          // déplacement de la cible depuis le fond (notre propre boucle)
    POINT dragStart_{};
    RECT dragFrom_{};
    HWINEVENTHOOK moveHook_ = nullptr;
    SIZE paintedSize_{};
    ULONGLONG bounceStart_ = 0;
    int probeRetries_ = 0;         // sondes interrompues (app occupée) reprises au plus 3 fois
    // Pendant une sonde, SendMessageTimeout laisse passer les messages envoyés à notre fil (WinEvent, activation) :
    // ils sont reportés après la sonde plutôt que traités au milieu d'elle.
    bool probing_ = false, attachPending_ = false, placePending_ = false;
    bool captureVisible_ = false;    // capture d'écran en cours : calques visibles aux captures
    HANDLE captureDone_ = nullptr;   // signalé par le fil une fois l'affichage des calques changé
    bool quitting_ = false;
    HINSTANCE instance_ = nullptr;
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<HWND> publicTarget_{nullptr};
    HWND pendingTarget_ = nullptr;
    LightsMode pendingMode_ = LightsMode::Standard;
    static TrafficWindow* self_;
};

} // namespace md
