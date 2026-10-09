// Forcer à quitter (plan 54) : la fenêtre « Forcer des applications à quitter » de macOS 26, au premier plan, sur le fil
// de son hôte (le Dock, ou la sonde d'essai réel). L'hôte fournit la liste, le réglage des icônes et l'action qui force
// une app à quitter (ou relance l'Explorateur) : la sonde lui en donne une qui ne peut arrêter que ses propres processus.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../icons/icon_provider.h"
#include "../ui/ui_draw.h"
#include "force_quit_logic.h"
#include "force_quit_view.h"

namespace md {

class ForceQuitWindow {
public:
    struct Env {
        std::function<std::vector<FqApp>()> list;                           // relue chaque seconde, fenêtre ouverte
        std::function<void(IconProvider&)> iconSetup;                       // style des icônes (fil des icônes)
        std::function<bool(const FqApp&, std::wstring* error)> forceQuit;   // arrêt, ou relance de l'Explorateur
        std::wstring shortcutLabel;                                         // « ⌥⊞⎋ » ; vide : pas de phrase
        std::optional<bool> dark;                                           // thème imposé ; sinon celui des apps
    };
    ForceQuitWindow() = default;
    ForceQuitWindow(const ForceQuitWindow&) = delete;
    ForceQuitWindow& operator=(const ForceQuitWindow&) = delete;
    ~ForceQuitWindow();

    // Ouvre la fenêtre sur l'écran du pointeur, ou la ramène au premier plan (liste relue) ; `preselect` : appId de l'app
    // choisie d'abord (celle qui était au premier plan), sinon aucune.
    bool open(HINSTANCE instance, Env env, const std::wstring& preselect = {});
    void close();
    bool isOpen() const { return hwnd_ != nullptr; }
    HWND hwnd() const { return hwnd_; }

    // État vu de l'extérieur : essais réels et journal.
    const std::vector<FqApp>& apps() const { return list_.apps; }
    int selected() const { return list_.selected; }
    bool sheetOpen() const { return sheet_.has_value(); }
    std::wstring sheetTitle() const { return sheet_ ? sheet_->spec.title : std::wstring(); }
    RECT rowOnScreen(int i) const;          // pixels de l'écran
    RECT buttonOnScreen() const;
    RECT sheetButtonOnScreen(int i) const;

private:
    struct Loader;
    struct Sheet {
        enum class Kind { Confirm, Error } kind = Kind::Confirm;
        ui::SheetSpec spec;
        ui::SheetLayout layout;
        FqApp target;          // l'app visée, retenue à l'ouverture de la feuille
        float progress = 0;    // apparition, de 0 à 1
        int hover = -1, pressed = -1;
    };

    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    bool createTarget();
    void render();
    void reload();
    void requestIcons();
    void select(int i);
    void openConfirm();
    void choose(int button);
    void closeSheet();
    void onMouse(UINT msg, float x, float y);
    void onKey(UINT vk, bool repeat);
    void updateTheme();
    RECT toScreen(D2D1_RECT_F r) const;

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    Env env_;
    float scale_ = 1;
    bool dark_ = false, active_ = true;
    FqListState list_;   // apps, sélection, défilement
    FqLayout layout_;
    std::optional<Sheet> sheet_;
    bool lightsHover_ = false, buttonPressed_ = false;
    int lightPressed_ = -1;
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    ui::FormatCache formats_;
    std::wstring font_;
    std::map<std::wstring, IconProvider::ImagePtr> images_;                   // par appId
    std::map<std::wstring, Microsoft::WRL::ComPtr<ID2D1Bitmap>> bitmaps_;    // par appId, pour la cible actuelle
    std::shared_ptr<Loader> loader_;
};

// Image de la fenêtre (exemple d'apps, une figée, une choisie), à l'échelle 2, sans rien afficher ;
// `sheet` : avec la feuille de confirmation. MacDock.exe --force-quit-snapshot.
bool forceQuitSnapshot(const std::wstring& path, bool dark, bool sheet);

} // namespace md
