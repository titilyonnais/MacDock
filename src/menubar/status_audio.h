// Son de la barre de menus : volume et sourdine de la sortie par défaut, liste des sorties, choix de la sortie par
// défaut (Core Audio). Rapide : utilisable sur le fil de la barre (COM initialisé, STA ou MTA).
#pragma once
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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace md
