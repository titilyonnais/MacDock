// Fenêtre de l'app Réglages MacDock, façon Réglages Système de macOS 27 : barre latérale translucide (fond acrylique de
// DWM), sections en groupes arrondis, contrôles dessinés en Direct2D sur une chaîne d'échange DirectComposition.
// Chaque changement est écrit tout de suite (settings_doc::commit) : le Dock et la barre l'appliquent en direct.
#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <optional>
#include <string>
#include <vector>

#include "../anim/spring.h"
#include "../settings/panes.h"
#include "../ui/ui_draw.h"
#include "../ui/ui_layout.h"

namespace md {

class SettingsWindow {
public:
    static constexpr wchar_t kClass[] = L"MacDockSettingsWindow";
    static UINT paneMessage();   // « MacDockSettingsPane » : wParam = indice de la section (seconde ouverture)

    ~SettingsWindow();
    bool create(HINSTANCE instance, const std::wstring& dataDir, PaneId pane);
    int run();   // boucle de messages, avec la surveillance du dossier des réglages

private:
    struct RowRef {
        int group = 0, row = 0;
    };
    // Géométrie d'une ligne à l'écran (points, défilement compris), refaite à chaque image.
    struct Geom {
        D2D1_RECT_F row{}, control{};
        float trackLeft = 0, trackRight = 0, cy = 0;
    };
    struct OpenMenu {
        int row = -1;
        D2D1_RECT_F rect{};
        int hover = -1;
    };

    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    bool initGraphics();
    void releaseTarget();
    bool createTarget();
    void resize();
    void render();
    void drawSidebar(ui::Painter& p, float h);
    void drawContent(ui::Painter& p, float w, float h);
    void drawRow(ui::Painter& p, int index);
    void computeGeometry(ui::Painter& p, float w);

    void selectPane(PaneId pane);
    void rebuildRows();             // lignes de la section, ressorts recalés sur le modèle
    void reloadModel();             // fichiers changés ailleurs
    void setValue(int index, double value, bool commitNow = true);
    void commitPending();           // curseur : dernière valeur écrite
    double valueOf(int index) const;
    bool enabled(int index) const;
    bool focusable(int index) const;
    const RowSpec& spec(int index) const;
    void openMenu(int index);
    void chooseMenu(int item);
    void animate();                 // minuterie tant qu'un ressort bouge ou que la barre de défilement s'efface
    void scrollBy(float points);
    void ensureVisible(int index);
    void updateDark();
    void buildEnv();
    POINT toPoints(LPARAM lp) const;
    int controlAt(float x, float y) const;
    int sidebarAt(float x, float y) const;
    int lightAt(float x, float y) const;
    float widthPt() const;
    float heightPt() const;
    void onMouseDown(float x, float y);
    void onMouseMove(float x, float y);
    void onMouseUp(float x, float y);
    bool onKey(WPARAM key);

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring dir_;
    float scale_ = 1;
    bool dark_ = false, active_ = true;

    SettingsModel model_;
    PaneEnv env_;
    PaneId pane_ = PaneId::Dock;
    std::vector<GroupSpec> groups_;
    std::vector<RowRef> rows_;
    std::vector<Geom> geoms_;
    std::vector<Spring> springs_;   // interrupteurs (une entrée par ligne)
    ui::PaneLayout layout_;
    std::vector<float> sidebarTops_;
    float scroll_ = 0;
    double scrollbarAlpha_ = 0;
    ULONGLONG scrollbarShownAt_ = 0;

    int hoverRow_ = -1, pressedRow_ = -1, focus_ = -1, sidebarHover_ = -1;
    bool draggingSlider_ = false, tracking_ = false;
    bool lightsHover_ = false;
    int lightsPressed_ = -1;
    std::optional<OpenMenu> menu_;
    std::optional<double> pending_;   // valeur de curseur pas encore écrite
    ULONGLONG lastCommit_ = 0, lastFrame_ = 0;
    bool animating_ = false;
    HANDLE change_ = INVALID_HANDLE_VALUE;
    HICON icon_ = nullptr, smallIcon_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D11Device> d3d_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2d_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> dc_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> visual_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    std::wstring font_;
    UINT pxW_ = 0, pxH_ = 0;
};

// Icône de l'app (tuile grise à engrenage), dessinée à la taille demandée ; nullptr si impossible.
HICON makeSettingsIcon(int size);

}  // namespace md
