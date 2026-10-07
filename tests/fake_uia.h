// Fournisseur UI Automation factice : une fenêtre hors écran, sur son propre fil, expose une barre de menus
// (MenuBar → MenuItem dépliables → entrées invocables), comme une app WinUI. Aucune entrée simulée, aucun affichage :
// déplier ne fait que rendre les enfants visibles dans l'arbre.
#pragma once
#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace test {

class UiaNode : public IRawElementProviderSimple,
                public IRawElementProviderFragment,
                public IRawElementProviderFragmentRoot,
                public IExpandCollapseProvider,
                public IInvokeProvider {
public:
    std::wstring name, accelerator, automationId;
    CONTROLTYPEID type = UIA_MenuItemControlTypeId;
    UiaNode* parent = nullptr;
    UiaNode* root = nullptr;
    std::vector<UiaNode*> children;
    std::atomic<bool> expanded{false};
    std::atomic<int> invoked{0};
    HWND hwnd = nullptr;   // racine seulement
    int id = 0;

    bool expandable() const { return type == UIA_MenuItemControlTypeId && !children.empty(); }
    bool isRoot() const { return parent == nullptr; }

    UiaNode* add(UiaNode* child) {
        static int next = 1;
        child->parent = this;
        child->root = root ? root : this;
        child->id = next++;
        children.push_back(child);
        return child;
    }
    // Enfants visibles : ceux d'une entrée dépliable seulement quand elle est dépliée.
    const std::vector<UiaNode*>& visible() const {
        static const std::vector<UiaNode*> none;
        return expandable() && !expanded ? none : children;
    }

    // IUnknown (jamais libéré : le test garde l'arbre jusqu'à la fin du processus)
    STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple))
            *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment))
            *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot) && isRoot())
            *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(IExpandCollapseProvider) && expandable())
            *out = static_cast<IExpandCollapseProvider*>(this);
        else if (riid == __uuidof(IInvokeProvider) && type == UIA_MenuItemControlTypeId && !expandable())
            *out = static_cast<IInvokeProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // IRawElementProviderSimple
    STDMETHODIMP get_ProviderOptions(ProviderOptions* r) override {
        *r = ProviderOptions(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return S_OK;
    }
    STDMETHODIMP GetPatternProvider(PATTERNID pattern, IUnknown** r) override {
        *r = nullptr;
        if (pattern == UIA_ExpandCollapsePatternId && expandable()) *r = static_cast<IExpandCollapseProvider*>(this);
        if (pattern == UIA_InvokePatternId && type == UIA_MenuItemControlTypeId && !expandable())
            *r = static_cast<IInvokeProvider*>(this);
        if (*r) AddRef();
        return S_OK;
    }
    STDMETHODIMP GetPropertyValue(PROPERTYID prop, VARIANT* r) override {
        VariantInit(r);
        auto str = [&](const std::wstring& s) {
            r->vt = VT_BSTR;
            r->bstrVal = SysAllocString(s.c_str());
        };
        switch (prop) {
            case UIA_ControlTypePropertyId: r->vt = VT_I4; r->lVal = type; break;
            case UIA_NamePropertyId: if (!name.empty()) str(name); break;
            case UIA_AcceleratorKeyPropertyId: if (!accelerator.empty()) str(accelerator); break;
            case UIA_AutomationIdPropertyId: if (!automationId.empty()) str(automationId); break;
            case UIA_IsEnabledPropertyId: r->vt = VT_BOOL; r->boolVal = VARIANT_TRUE; break;
            default: break;
        }
        return S_OK;
    }
    STDMETHODIMP get_HostRawElementProvider(IRawElementProviderSimple** r) override {
        *r = nullptr;
        return isRoot() ? UiaHostProviderFromHwnd(hwnd, r) : S_OK;
    }

    // IRawElementProviderFragment
    STDMETHODIMP Navigate(NavigateDirection dir, IRawElementProviderFragment** r) override {
        *r = nullptr;
        UiaNode* n = nullptr;
        if (dir == NavigateDirection_Parent) {
            n = parent;
        } else if (dir == NavigateDirection_FirstChild || dir == NavigateDirection_LastChild) {
            const auto& v = visible();
            if (!v.empty()) n = dir == NavigateDirection_FirstChild ? v.front() : v.back();
        } else if (parent) {
            const auto& v = parent->visible();
            for (size_t i = 0; i < v.size(); ++i)
                if (v[i] == this) {
                    if (dir == NavigateDirection_NextSibling && i + 1 < v.size()) n = v[i + 1];
                    if (dir == NavigateDirection_PreviousSibling && i > 0) n = v[i - 1];
                }
        }
        if (n) *r = static_cast<IRawElementProviderFragment*>(n);
        return S_OK;
    }
    STDMETHODIMP GetRuntimeId(SAFEARRAY** r) override {
        *r = nullptr;
        if (isRoot()) return S_OK;
        int ids[] = {UiaAppendRuntimeId, id};
        *r = SafeArrayCreateVector(VT_I4, 0, 2);
        for (LONG i = 0; i < 2; ++i) SafeArrayPutElement(*r, &i, &ids[i]);
        return S_OK;
    }
    STDMETHODIMP get_BoundingRectangle(UiaRect* r) override {
        *r = {-3000, -3000, 100, 20};
        return S_OK;
    }
    STDMETHODIMP GetEmbeddedFragmentRoots(SAFEARRAY** r) override {
        *r = nullptr;
        return S_OK;
    }
    STDMETHODIMP SetFocus() override { return S_OK; }
    STDMETHODIMP get_FragmentRoot(IRawElementProviderFragmentRoot** r) override {
        *r = static_cast<IRawElementProviderFragmentRoot*>(root ? root : this);
        return S_OK;
    }

    // IRawElementProviderFragmentRoot
    STDMETHODIMP ElementProviderFromPoint(double, double, IRawElementProviderFragment** r) override {
        *r = nullptr;
        return S_OK;
    }
    STDMETHODIMP GetFocus(IRawElementProviderFragment** r) override {
        *r = nullptr;
        return S_OK;
    }

    // IExpandCollapseProvider
    STDMETHODIMP Expand() override {
        expanded = true;
        return S_OK;
    }
    STDMETHODIMP Collapse() override {
        expanded = false;
        return S_OK;
    }
    STDMETHODIMP get_ExpandCollapseState(ExpandCollapseState* r) override {
        *r = expandable() ? (expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed)
                          : ExpandCollapseState_LeafNode;
        return S_OK;
    }

    // IInvokeProvider : comme un vrai menu, invoquer referme toute la chaîne.
    STDMETHODIMP Invoke() override {
        ++invoked;
        for (UiaNode* p = parent; p; p = p->parent) p->expanded = false;
        return S_OK;
    }
};

inline UiaNode* uiaNode(const wchar_t* name, CONTROLTYPEID type = UIA_MenuItemControlTypeId, const wchar_t* key = L"") {
    auto* n = new UiaNode;   // jamais libéré (voir AddRef)
    n->name = name;
    n->type = type;
    n->accelerator = key;
    return n;
}

// Fenêtre d'app hors écran dont l'arbre UI Automation est root (son fil traite WM_GETOBJECT).
struct UiaApp {
    UiaNode* root = nullptr;
    HWND hwnd = nullptr;
    std::thread thread;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    static inline UiaNode* current = nullptr;

    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_GETOBJECT && static_cast<long>(lp) == static_cast<long>(UiaRootObjectId) && current)
            return UiaReturnRawElementProvider(h, wp, lp, current);
        if (msg == WM_DESTROY) UiaReturnRawElementProvider(h, 0, 0, nullptr);
        return DefWindowProcW(h, msg, wp, lp);
    }
    explicit UiaApp(UiaNode* tree) : root(tree) {
        current = tree;
        thread = std::thread([this] {
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            WNDCLASSW wc{};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"MacDockTestUiaApp";
            RegisterClassW(&wc);
            hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"App UIA", WS_OVERLAPPEDWINDOW, -3000, -3000, 200,
                                   200, nullptr, nullptr, wc.hInstance, nullptr);
            root->hwnd = hwnd;
            SetEvent(ready);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
            DestroyWindow(hwnd);
            CoUninitialize();
        });
        WaitForSingleObject(ready, 5000);
    }
    ~UiaApp() {
        PostThreadMessageW(GetThreadId(thread.native_handle()), WM_QUIT, 0, 0);
        thread.join();
        CloseHandle(ready);
        current = nullptr;
    }
    UiaApp(const UiaApp&) = delete;
    UiaApp& operator=(const UiaApp&) = delete;
};

} // namespace test
