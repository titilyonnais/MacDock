// Coup d'œil : vrais aperçus dans la fenêtre, sur le fil de la fenêtre (STA).
// - PreviewHost : gestionnaire d'aperçu du Shell (PDF, Word, Excel, PowerPoint, HTML, polices…), toujours hors de notre
//   processus (prevhost.exe) : un gestionnaire défaillant ne fait pas tomber MacDock.
// - MediaHost : vidéo ou son lu par Media Foundation (MFPlay), lecture lancée tout de suite, comme Quick Look.
#pragma once
#include <windows.h>
#include <mfplay.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <string>

namespace md {

// CLSID du gestionnaire d'aperçu enregistré pour l'extension de path ; false s'il n'y en a pas.
bool previewHandlerFor(const std::wstring& path, CLSID& clsid);

class PreviewHost {
public:
    ~PreviewHost() { close(); }
    // Aperçu dessiné dans rc (client de parent). false : rien n'est affiché (la miniature reste).
    bool open(HINSTANCE instance, HWND parent, const RECT& rc, const std::wstring& path, const CLSID& clsid, bool dark);
    void resize(const RECT& rc);
    void close();   // Unload, puis libération (prevhost garde le processus pour les suivants)
    bool active() const { return handler_ != nullptr; }

private:
    // Fenêtre enfant à la place de l'aperçu : certains gestionnaires (Word) occupent tout leur parent, barre d'outils
    // comprise, quel que soit le rectangle donné.
    HWND box_ = nullptr;
    Microsoft::WRL::ComPtr<IPreviewHandler> handler_;
};

class MediaHost {
public:
    ~MediaHost() { close(); }
    // video : fenêtre enfant dans rc ; sinon, son seul (pas de fenêtre).
    bool open(HINSTANCE instance, HWND parent, const RECT& rc, const std::wstring& path, bool video);
    void resize(const RECT& rc);
    void toggle();   // lecture ⇄ pause
    void close();    // le son s'arrête toujours
    bool active() const { return player_ != nullptr; }
    bool playing() const;
    bool paused() const { return paused_; }   // mis en pause par un clic (pas : pas encore lancé, ou fini)

private:
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    HWND video_ = nullptr;
    bool paused_ = false;
    Microsoft::WRL::ComPtr<IMFPMediaPlayer> player_;
};

} // namespace md
