// Coup d'œil (Quick Look de macOS) : fenêtre d'aperçu du fichier sélectionné dans l'Explorateur ou sur le bureau.
// Elle ne prend jamais le focus (l'Explorateur reste actif, ses flèches changent la sélection et l'aperçu la suit) ;
// les touches Espace, Échap et Entrée lui parviennent par le crochet clavier du Dock.
#pragma once
#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace md {

class QuickLookWindow {
public:
    ~QuickLookWindow();
    // Montre paths[index] (sélection de la fenêtre owner) ; remplace l'aperçu déjà ouvert.
    void show(HINSTANCE instance, std::vector<std::wstring> paths, std::size_t index, HWND owner);
    void close();
    bool isOpen() const { return open_.load(); }
    HWND owner() const { return owner_; }
    const std::vector<std::wstring>& paths() const { return paths_; }
    void openFile();   // Entrée ou « Ouvrir avec » : l'app par défaut, puis fermeture

    // Contenu préparé sur un fil à part (miniature, texte, renseignements).
    struct Content {
        enum class Kind { Image, Text, Icon } kind = Kind::Icon;
        std::vector<std::uint8_t> pixels;   // BGRA prémultiplié, haut en premier
        SIZE size{};                       // pixels de l'image
        std::wstring text;                 // fichier texte
        std::wstring name, kindName, details, openWith;
        std::uint64_t generation = 0;
    };

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    bool ensureWindow();
    void startLoad();
    void place();
    void render();
    int hitButton(POINT client) const;   // 0 aucun, 1 fermer, 2 ouvrir

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr, owner_ = nullptr;
    std::atomic<bool> open_{false};
    std::vector<std::wstring> paths_;
    std::size_t index_ = 0;
    std::atomic<std::uint64_t> generation_{0};
    std::unique_ptr<Content> content_;
    bool dark_ = false;
    float scale_ = 1;
    int hover_ = 0;
    RECT closeRc_{}, openRc_{};
    Microsoft::WRL::ComPtr<ID2D1Factory> d2d_;
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_;
    Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> rt_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap_;
};

} // namespace md
