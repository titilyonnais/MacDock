// Capture unique d'une fenêtre, même réduite, pour le génie GPU. Windows.Graphics.Capture ne donne aucune image
// d'une fenêtre réduite, mais DWM garde sa miniature : une fenêtre relais, hors de tous les écrans, porte cette
// miniature à taille réelle, et c'est le relais qui est capturé (première image en 10 à 20 ms).
#pragma once
#include <windows.h>
#include <d3d11.h>

#include <functional>
#include <memory>
#include <string>

namespace md {

class WindowCapture {
public:
    WindowCapture();
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    static bool supported();   // Windows.Graphics.Capture présent (Windows 10 1903+)
    void prepare(HINSTANCE instance);   // relais créé d'avance (caché) : pas d'attente à la première capture
    // Relais + capture lancés pour source ; false si impossible (rien ne reste ouvert). dev : multithread protégé.
    bool start(HINSTANCE instance, ID3D11Device* dev, HWND source);
    // Si une image est arrivée : use(texture BGRA prémultipliée du device, largeur, hauteur du contenu), puis true.
    bool poll(const std::function<void(ID3D11Texture2D*, UINT, UINT)>& use);
    void stop();   // ferme la capture, retire la miniature, cache le relais
    bool active() const;
    HMONITOR monitor() const;   // écran le plus proche du relais (son blanc SDR est celui de la capture)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Sonde (MacDock.exe --genie-capture-probe f.png) : fenêtre outil hors écran, réduite sans activation, capturée
// par le relais, déformée par le génie GPU à t = 0,5 et écrite en PNG. Aucune fenêtre n'est visible.
bool genieCaptureProbe(HINSTANCE instance, const std::wstring& png);
// Fenêtre factice (outil, en couches, 640 x 400) posée hors de tous les écrans puis réduite sans activation.
HWND createMinimizedProbe(HINSTANCE instance);

} // namespace md
