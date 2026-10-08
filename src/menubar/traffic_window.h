// Feux tricolores à l'écran : un petit calque posé sur la barre de titre de la fenêtre active, juste au-dessus
// d'elle dans l'ordre d'affichage, qui la suit quand elle bouge et lui envoie ses commandes système. Les boutons
// réduire / agrandir / fermer de Windows (ou ceux que l'app dessine elle-même) sont repérés en sondant la fenêtre
// (WM_NCHITTEST) : pastilles à gauche et un second calque qui les cache quand la gauche de la barre est libre,
// sinon pastilles posées à leur place.
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
    // Toujours à gauche (réglage par défaut) ou, si la gauche est prise, sur les boutons de Windows.
    void setAlwaysLeft(bool on);

private:
    enum class Spot { None, Left, Over };   // pas de pastilles, à gauche (+ cache), sur les boutons de Windows
    struct Placement {
        HWND target = nullptr;
        SIZE size{};
        bool zoomed = false, valid = false;
        UINT dpi = 0;
        Spot spot = Spot::None;
        RECT buttons{};          // boutons de Windows, relatifs au coin haut droit du cadre
        LONG titleBottom = 0;    // bas de la barre de titre, relatif au haut du cadre
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
    void raise();                // juste au-dessus de la cible (le cache sous les pastilles)
    void unhook();

    HWND hwnd_ = nullptr, cover_ = nullptr, target_ = nullptr;
    HWINEVENTHOOK hook_ = nullptr;
    LightsMode mode_ = LightsMode::Standard;
    LightsLayout layout_{}, coverLayout_{};
    LightsState state_{};
    Placement placement_{};
    Spot spot_ = Spot::None;
    double scale_ = 1;
    bool shown_ = false, tracking_ = false, painted_ = false, coverPainted_ = false;
    int pressed_ = -1;
    bool dragging_ = false;          // déplacement de la cible depuis le fond (notre propre boucle)
    POINT dragStart_{};
    RECT dragFrom_{};
    HWINEVENTHOOK moveHook_ = nullptr;
    SIZE paintedSize_{}, coverSize_{};
    ULONGLONG bounceStart_ = 0;
    int probeRetries_ = 0;         // sondes interrompues (app occupée) reprises au plus 3 fois
    // Pendant une sonde, SendMessageTimeout laisse passer les messages envoyés à notre fil (WinEvent, activation) :
    // ils sont reportés après la sonde plutôt que traités au milieu d'elle.
    bool probing_ = false, attachPending_ = false, placePending_ = false;
    bool alwaysLeft_ = true;
    bool quitting_ = false;
    HINSTANCE instance_ = nullptr;
    std::thread thread_;
    std::atomic<DWORD> threadId_{0};
    std::atomic<HWND> publicTarget_{nullptr};
    std::uint32_t coverColor_ = 0;   // couleur juste à gauche des boutons cachés, à mi-hauteur (Mica : plus foncée en haut)
    HWND pendingTarget_ = nullptr;
    LightsMode pendingMode_ = LightsMode::Standard;
    static TrafficWindow* self_;
};

} // namespace md
