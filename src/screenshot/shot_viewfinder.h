// Viseur de ⊞⇧4, comme ⌘⇧4 sur macOS : curseur en croix avec ses coordonnées, zone tirée à la souris ; Espace
// passe en mode fenêtre (voile bleu sur la fenêtre survolée, curseur appareil photo), Échap annule.
// Fenêtres en couches, sans activation et exclues des captures : la copie d'écran se fait sans les retirer.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <functional>
#include <vector>

#include "../app/sprite_window.h"
#include "screenshot_logic.h"

namespace md {

class ShotViewfinder {
public:
    struct Result {
        enum class Kind { Cancel, Region, Window } kind = Kind::Cancel;
        RECT rect{};             // Region : pixels écran ; Window : bords visibles de la fenêtre
        HWND window = nullptr;   // Window
        bool shadow = true;      // Window : ombre de macOS (Alt au clic : sans ombre)
        HMONITOR monitor = nullptr;
    };
    using Done = std::function<void(const Result&)>;

    ~ShotViewfinder();
    // ignore : fenêtres jamais visées en mode fenêtre (les nôtres). done est appelé une fois, viseur déjà retiré.
    bool start(HINSTANCE instance, Done done, std::function<bool(HWND)> ignore);
    void key(ShotSessionKey k);   // Échap, Espace (pris par le crochet clavier du Dock)
    bool active() const { return active_; }
    void cancel();

private:
    // Voile uni translucide (sélection, fenêtre visée) : transparent aux clics, déplacé sans redessiner.
    struct Tint {
        HWND hwnd = nullptr;
        bool create(HINSTANCE instance, COLORREF fill, COLORREF border, BYTE alpha);
        void show(const RECT& r, int radius);
        void hide();
        void destroy();
        RECT shown{};
        int radius = -1;
    };
    static LRESULT CALLBACK catcherProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK tintProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp);
    void onMove(POINT pt);
    void showLabel(POINT pt, int a, int b);   // deux nombres (points) près du curseur
    void hover(POINT pt);                     // mode fenêtre : fenêtre visée sous le point
    void finish(const Result& r);
    void teardown();
    double scaleAt(POINT pt) const;

    HINSTANCE instance_ = nullptr;
    bool active_ = false, windowMode_ = false, dragging_ = false;
    std::vector<HWND> catchers_;   // un par écran
    Tint selection_, highlight_;
    SpriteWindow label_;
    POINT anchor_{};
    RECT bounds_{};   // écran où la sélection a commencé
    HWND hovered_ = nullptr;
    HCURSOR cross_ = nullptr, camera_ = nullptr;
    Done done_;
    std::function<bool(HWND)> ignore_;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<IWICImagingFactory> wic_;
};

} // namespace md
