// Coup d'œil (Quick Look de macOS) : fenêtre d'aperçu du fichier sélectionné dans l'Explorateur ou sur le bureau.
// Elle ne prend jamais le focus (l'Explorateur reste actif, ses flèches changent la sélection et l'aperçu la suit) ;
// les touches Espace, Échap et Entrée lui parviennent par le crochet clavier du Dock.
// Elle vit sur son propre fil : un aperçu de document (prevhost.exe) ou une vidéo qui s'ouvre lentement ne fige
// jamais le Dock.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "quicklook_hosts.h"
#include "quicklook_logic.h"

namespace md {

class QuickLookWindow {
public:
    ~QuickLookWindow();
    // Fil du Dock. Montre paths[index] (sélection de la fenêtre owner) ; remplace l'aperçu déjà ouvert.
    void show(HINSTANCE instance, std::vector<std::wstring> paths, std::size_t index, HWND owner);
    void close();
    bool isOpen() const { return open_.load(); }
    HWND owner() const { return owner_; }                               // fil du Dock
    const std::vector<std::wstring>& paths() const { return paths_; }   // fil du Dock

    // Contenu préparé sur un fil à part (miniature, texte, renseignements).
    struct Content {
        enum class Kind { Image, Text, Icon } kind = Kind::Icon;
        std::vector<std::uint8_t> pixels;   // BGRA prémultiplié, haut en premier
        SIZE size{};                       // pixels de l'image
        std::wstring text;                 // fichier texte
        std::wstring name, kindName, details, openWith;
        QuickLookMedia media = QuickLookMedia::None;
        bool shellPreview = false;         // un gestionnaire d'aperçu du Shell est enregistré
        CLSID previewClsid{};
        std::uint64_t generation = 0;
    };

private:
    struct Request {
        std::wstring path;
        std::uint64_t generation = 0;
        float scale = 1;
        HWND target = nullptr;
    };
    struct ShowRequest {
        std::wstring path;
        HWND owner = nullptr;
    };

    // ---- Fil de la fenêtre ----
    bool startThread(HINSTANCE instance);
    void threadMain(HINSTANCE instance, std::promise<bool>* ready);
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void onShow(const ShowRequest& r);
    void onClose();
    void openFile();
    void startLoad();
    void workerLoop();
    void place();
    RECT targetRect();           // rectangle écran voulu pour le contenu
    RECT contentRect() const;    // client, sous la barre d'outils
    void render();
    int hitButton(POINT client) const;   // 0 aucun, 1 fermer, 2 ouvrir, 3 plein écran
    void startHosts();           // aperçu du Shell ou média, une fois la fenêtre en place
    void stopHosts();
    void stepAnimation();
    void toggleFullscreen();

    // Fil du Dock
    HWND owner_ = nullptr;
    std::vector<std::wstring> paths_;
    std::atomic<bool> open_{false};
    std::thread ui_;

    // Fil de la fenêtre
    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr, ownerUi_ = nullptr;
    std::wstring path_;
    std::atomic<std::uint64_t> generation_{0};
    std::unique_ptr<Content> content_;
    bool dark_ = false, fullscreen_ = false, visible_ = false;
    float scale_ = 1;
    int hover_ = 0;
    RECT closeRc_{}, openRc_{}, fullRc_{};
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
    PreviewHost preview_;
    MediaHost media_;
    // Appel COM sortant (DoPreview…) : la boucle modale de COM peut livrer nos messages ; ils sont rejoués après.
    bool inHostCall_ = false;
    std::vector<MSG> deferred_;
    // Ouverture en zoom depuis l'icône du fichier.
    bool animating_ = false;
    ULONGLONG animStart_ = 0;
    RECT animFrom_{}, animTo_{};

    // Fil de chargement des miniatures (un seul, dernière demande seulement)
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::optional<Request> pending_;
    bool stop_ = false;
};

} // namespace md
