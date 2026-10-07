#include "drop_target.h"

#include <shellapi.h>
#include <shlobj.h>

namespace md {

DropTarget::DropTarget(HWND hwnd, Callbacks cb) : hwnd_(hwnd), cb_(std::move(cb)) {
    CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&helper_));
}

HRESULT DropTarget::QueryInterface(REFIID riid, void** out) {
    if (!out) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDropTarget) {
        *out = static_cast<IDropTarget*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG DropTarget::Release() {
    ULONG n = --refs_;
    if (n == 0) delete this;
    return n;
}

std::vector<std::wstring> DropTarget::pathsOf(IDataObject* data) {
    std::vector<std::wstring> out;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    if (!data || FAILED(data->GetData(&fmt, &med))) return out;
    if (auto drop = static_cast<HDROP>(GlobalLock(med.hGlobal))) {
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; ++i) {
            std::wstring p(DragQueryFileW(drop, i, nullptr, 0), L'\0');
            DragQueryFileW(drop, i, p.data(), UINT(p.size() + 1));
            out.push_back(std::move(p));
        }
        GlobalUnlock(med.hGlobal);
    }
    ReleaseStgMedium(&med);
    return out;
}

DWORD DropTarget::decide(POINTL pt, DWORD allowed, DropAction* action) {
    DropAction a = cb_.over ? cb_.over(paths_, POINT{pt.x, pt.y}) : DropAction::None;
    DWORD effect = chooseEffect(a, allowed);
    if (action) *action = effect == DROPEFFECT_NONE ? DropAction::None : a;
    return effect;
}

void DropTarget::setDropEffectFormat(IDataObject* data, const wchar_t* format, DWORD effect) {
    FORMATETC fmt{CLIPFORMAT(RegisterClipboardFormatW(format)), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM med{};
    med.tymed = TYMED_HGLOBAL;
    med.hGlobal = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (!med.hGlobal) return;
    if (auto p = static_cast<DWORD*>(GlobalLock(med.hGlobal))) {
        *p = effect;
        GlobalUnlock(med.hGlobal);
    }
    if (FAILED(data->SetData(&fmt, &med, TRUE))) ReleaseStgMedium(&med);
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD, POINTL pt, DWORD* effect) {
    paths_ = pathsOf(data);
    *effect = decide(pt, *effect);
    if (helper_) {
        POINT p{pt.x, pt.y};
        helper_->DragEnter(hwnd_, data, &p, *effect);
    }
    return S_OK;
}

HRESULT DropTarget::DragOver(DWORD, POINTL pt, DWORD* effect) {
    *effect = decide(pt, *effect);
    if (helper_) {
        POINT p{pt.x, pt.y};
        helper_->DragOver(&p, *effect);
    }
    return S_OK;
}

HRESULT DropTarget::DragLeave() {
    if (helper_) helper_->DragLeave();
    if (cb_.leave) cb_.leave();
    paths_.clear();
    return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD, POINTL pt, DWORD* effect) {
    if (paths_.empty()) paths_ = pathsOf(data);
    DropAction action = DropAction::None;
    DWORD chosen = decide(pt, *effect, &action);
    if (helper_) {
        POINT p{pt.x, pt.y};
        helper_->Drop(data, &p, chosen);
    }
    if (dockPerformsMove(action) && data) {
        // Déplacement fait par le Dock après le retour de Drop : la source ne doit pas supprimer l'original
        // (« optimized move » du Shell : effet réalisé NONE, effet logique MOVE).
        setDropEffectFormat(data, CFSTR_PERFORMEDDROPEFFECT, DROPEFFECT_NONE);
        setDropEffectFormat(data, CFSTR_LOGICALPERFORMEDDROPEFFECT, DROPEFFECT_MOVE);
    }
    *effect = dropReturnEffect(action, chosen);
    if (action != DropAction::None && cb_.drop) cb_.drop(paths_, POINT{pt.x, pt.y});
    if (cb_.leave) cb_.leave();
    paths_.clear();
    return S_OK;
}

} // namespace md
