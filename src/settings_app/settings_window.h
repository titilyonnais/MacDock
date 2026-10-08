// Fenêtre de l'app Réglages MacDock, façon Réglages Système de macOS 26 : barre latérale translucide (fond acrylique de
// DWM), sections en groupes arrondis, contrôles dessinés en Direct2D sur une chaîne d'échange DirectComposition.
// Chaque changement est écrit tout de suite (settings_doc::commit) : le Dock et la barre l'appliquent en direct.
// Recherche dans la barre latérale, raccourcis saisis au clavier, boutons d'action (fil de fond), feuilles d'alerte.
#pragma once
#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "../anim/spring.h"
#include "../settings/actions.h"
#include "../settings/panes.h"
#include "../ui/ui_draw.h"
#include "../ui/ui_layout.h"

namespace md {

class SettingsWindow {
public:
    static UINT paneMessage();   // « MacDockSettingsPane » : wParam = indice de la section (seconde ouverture)

    ~SettingsWindow();
    // testMode (--data) : démarrage avec Windows dans un fichier d'essai ; les actions qui touchent au vrai système
    // (quitter MacDock, installateurs, dossiers ouverts) sont seulement écrites au journal.
    bool create(HINSTANCE instance, const std::wstring& dataDir, PaneId pane, bool testMode = false);
    int run();   // boucle de messages, avec la surveillance du dossier des réglages

private:
    struct RowRef {
        int group = 0, row = 0;
    };
    // Géométrie d'une ligne à l'écran (points, défilement compris), refaite à chaque image.
    struct Geom {
        D2D1_RECT_F row{}, control{};
        float trackLeft = 0, trackRight = 0, cy = 0;
        std::vector<D2D1_RECT_F> buttons;   // ligne de boutons : un rectangle par bouton
    };
    struct OpenMenu {
        int row = -1;
        D2D1_RECT_F rect{};
        int hover = -1;
    };
    // Feuille d'alerte : modale, elle descend de la barre de titre ; `done` reçoit le bouton choisi.
    struct OpenSheet {
        ui::SheetSpec spec;
        ui::SheetLayout layout;          // refaite à chaque image
        std::function<void(int)> done;
        Spring appear;
        int pressed = -1;
        int cancel = -1;                 // bouton d'Échap (-1 : aucun)
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
    void endPress();                // glisser ou appui terminé (curseur écrit une fois), capture rendue
    void recreateGraphics();        // périphérique perdu (pilote mis à jour, TDR) : tout est refait
    void reportWrite(bool ok);      // écriture ratée : le modèle reprend ce que disent les fichiers, et on le dit
    void updateProblem();           // message si un fichier de réglages est invalide ou illisible
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
    void onMouseMove(float x, float y, bool buttonDown);
    void onMouseUp(float x, float y);
    bool onKey(WPARAM key, bool repeat = false);

    // Recherche.
    void setQuery(std::wstring query);
    void refreshSidebar();               // sections visibles, leurs positions, lignes soulignées
    // Boutons, feuilles, actions.
    int buttonAt(int row, float x, float y) const;
    void runAction(ButtonSpec button);
    void runCommands(std::vector<ActionCommand> commands, bool refreshEnv);
    void exportTo();
    void importFrom();
    std::optional<std::wstring> fileDialog(bool save);
    void openSheet(ui::SheetSpec spec, int cancel, std::function<void(int)> done);
    void closeSheet(int choice);
    void showMessage(std::wstring title, std::wstring message);
    // Enregistreur de raccourci.
    void startRecording(int row);
    void stopRecording();
    void releaseRecordHook(bool force);   // le crochet part quand plus aucune touche gardée n'attend sa relâche
    void onRecordKey(UINT vk, UINT mods);
    void commitShortcut(int row, const std::wstring& text);
    static LRESULT CALLBACK recordHook(int code, WPARAM wp, LPARAM lp);
    static SettingsWindow* recordTarget_;

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    std::wstring dir_;
    float scale_ = 1;
    bool dark_ = false, active_ = true;

    SettingsModel model_;
    ModelFiles files_;              // fichiers invalides ou illisibles : jamais réécrits
    std::wstring problem_;          // affiché dans la zone de titre
    bool reloadPending_ = false;    // fichiers changés pendant un glisser : relus à la fin
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

    bool testMode_ = false;
    std::wstring exeDir_;
    SettingsIo io_;                       // démarrage avec Windows (registre, ou fichier d'essai)
    std::wstring query_;
    bool searchFocused_ = false;
    std::vector<PaneMatch> matches_;
    std::vector<int> visible_;            // indices de paneList() dans la barre latérale
    std::vector<int> highlight_;          // lignes trouvées par la recherche, dans la section affichée
    std::optional<OpenSheet> sheet_;
    int recording_ = -1;                  // ligne de raccourci en écoute
    std::wstring recordNote_;             // « Windows garde ce raccourci »
    HHOOK recordHook_ = nullptr;
    int pressedButton_ = -1, focusButton_ = 0;
    bool busy_ = false;                   // une action tourne (installateur…)
    std::thread worker_;                  // son fil : attendu avant la fin du processus (« Relancer » doit aboutir)

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
