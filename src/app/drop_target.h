// Cible de glisser-déposer OLE du Dock : fichiers (CF_HDROP) venus de l'Explorateur ou d'une autre app.
#pragma once
#include <windows.h>
#include <ole2.h>
#include <shobjidl.h>
#include <wrl/client.h>

#include <atomic>
#include <functional>
#include <string>
#include <vector>

#include "../interact/drop_rules.h"

namespace md {

class DropTarget final : public IDropTarget {
public:
    struct Callbacks {
        // Survol : action visée (None si refusé). Point en coordonnées écran.
        std::function<DropAction(const std::vector<std::wstring>& paths, POINT screen)> over;
        std::function<void()> leave;
        std::function<void(const std::vector<std::wstring>& paths, POINT screen)> drop;
    };
    DropTarget(HWND hwnd, Callbacks cb);

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override;
    // IDropTarget
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt, DWORD* effect) override;

    static std::vector<std::wstring> pathsOf(IDataObject* data);

private:
    DWORD decide(POINTL pt, DWORD allowed, DropAction* action = nullptr);
    static void setDropEffectFormat(IDataObject* data, const wchar_t* format, DWORD effect);

    std::atomic<ULONG> refs_{1};
    HWND hwnd_;
    Callbacks cb_;
    std::vector<std::wstring> paths_;
    Microsoft::WRL::ComPtr<IDropTargetHelper> helper_;   // image glissée dessinée par le Shell
};

} // namespace md
