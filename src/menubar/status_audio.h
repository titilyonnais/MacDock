// Son de la barre de menus : volume et sourdine de la sortie par défaut, liste des sorties, choix de la sortie par
// défaut (Core Audio). Rapide : utilisable sur le fil de la barre (COM initialisé, STA ou MTA).
#pragma once
#include <windows.h>
#include <unknwn.h>
#include <wrl/client.h>

#include <memory>
#include <string>
#include <vector>

namespace md {

struct AudioOutput {
    std::wstring id, name;
    bool isDefault = false;
};

class AudioStatus {
public:
    AudioStatus();
    ~AudioStatus();
    AudioStatus(const AudioStatus&) = delete;
    AudioStatus& operator=(const AudioStatus&) = delete;

    bool init();               // false : pas de Core Audio ou pas de sortie
    float volume();            // 0..1 de la sortie par défaut (relue à chaque appel : elle peut changer), -1 sinon
    bool muted();
    bool setVolume(float v);   // borné à [0, 1] ; enlève la sourdine
    bool setMuted(bool m);
    std::vector<AudioOutput> outputs();          // sorties actives, la sortie par défaut marquée
    bool setDefault(const std::wstring& id);     // tous rôles ; false si l'identifiant est inconnu
    // Volume, sourdine ou sortie par défaut changés (ailleurs aussi) : msg posté à hwnd, depuis un fil de Core Audio.
    // À rappeler quand msg arrive avec wParam 1 : l'abonnement suit alors la nouvelle sortie par défaut.
    bool watch(HWND hwnd, UINT msg);
    void unwatch();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Objet de notification (IAudioEndpointVolumeCallback et IMMNotificationClient) qui poste msg à hwnd : wParam 0 pour
// le volume ou la sourdine, 1 pour un changement de sortie par défaut.
Microsoft::WRL::ComPtr<IUnknown> makeAudioNotifier(HWND hwnd, UINT msg);

} // namespace md
