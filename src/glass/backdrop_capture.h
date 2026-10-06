// Capture de l'arrière-plan du Dock par Desktop Duplication, sur un thread dédié.
// La dernière image de la région est publiée dans une texture partagée (handle NT + keyed mutex),
// que le thread d'interface copie dans sa propre texture avec takeLatest().
#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <thread>

#include "capture_policy.h"

namespace md {

class BackdropCapture {
    template <class T> using Com = Microsoft::WRL::ComPtr<T>;

public:
    enum class Status { Off, Running, Unavailable };
    // Fournit la texture de destination (device de l'interface) pour une image w x h ; nullptr si impossible.
    using DestinationFn = std::function<ID3D11Texture2D*(UINT w, UINT h, bool scRgb)>;

    ~BackdropCapture() { stop(); }
    // notify reçoit notifyMsg quand une nouvelle image couvre la région (au plus un message en attente).
    bool start(HWND notify, UINT notifyMsg, HMONITOR monitor, IRect regionScreen);
    void setRegion(IRect regionScreen);   // provoque une copie complète
    void stop();
    Status status() const { return status_.load(); }
    // Thread d'interface : copie la dernière image dans la texture fournie par dst. false si rien de neuf.
    bool takeLatest(ID3D11Device* uiDevice, ID3D11DeviceContext* ctx, const DestinationFn& dst, bool& scRgb,
                    float& sdrWhite);
    static Com<IDXGIAdapter1> adapterFor(HMONITOR monitor);   // carte qui pilote l'écran (nullptr si introuvable)

private:
    void run();
    bool captureLoop(Com<ID3D11Device>& dev, Com<IDXGIOutputDuplication>& dup, const IRect& outputDesktop);
    void releaseShared();
    static float querySdrWhite(HMONITOR monitor);

    HWND notify_ = nullptr;
    UINT notifyMsg_ = 0;
    HMONITOR monitor_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::atomic<Status> status_{Status::Off};
    std::atomic<bool> pending_{false};
    std::atomic<float> sdrWhite_{1.0f};

    std::mutex lock_;   // protège les champs ci-dessous
    IRect region_{};
    bool regionChanged_ = true;
    HANDLE sharedHandle_ = nullptr;
    UINT generation_ = 0, sharedW_ = 0, sharedH_ = 0;
    bool sharedScRgb_ = false;
    bool hasFrame_ = false;

    // Côté capture (thread de capture uniquement).
    Com<ID3D11Texture2D> shared_;
    Com<IDXGIKeyedMutex> sharedMutex_;
    DXGI_FORMAT sharedFormat_ = DXGI_FORMAT_UNKNOWN;

    // Côté interface (thread d'interface uniquement).
    Com<ID3D11Texture2D> uiShared_;
    Com<IDXGIKeyedMutex> uiMutex_;
    UINT uiGeneration_ = 0;
    ID3D11Device* uiDevice_ = nullptr;
};

} // namespace md
