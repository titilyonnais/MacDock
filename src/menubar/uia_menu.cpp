#include "uia_menu.h"

#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

#include <memory>

#include "../core/log.h"

namespace md {
namespace {

template <class T> using Com = Microsoft::WRL::ComPtr<T>;

constexpr DWORD kPopupWaitMs = 800;      // apparition du menu déplié
constexpr ULONGLONG kReadBudgetMs = 1500;   // lecture d'un menu et de ses sous-menus

bool startsWith(std::wstring_view s, std::wstring_view prefix) { return s.substr(0, prefix.size()) == prefix; }

Com<IUIAutomationCondition> typeIs(IUIAutomation* u, CONTROLTYPEID type) {
    VARIANT v{};
    v.vt = VT_I4;
    v.lVal = type;
    Com<IUIAutomationCondition> c;
    u->CreatePropertyCondition(UIA_ControlTypePropertyId, v, &c);
    return c;
}

Com<IUIAutomationCondition> either(IUIAutomation* u, CONTROLTYPEID a, CONTROLTYPEID b) {
    Com<IUIAutomationCondition> c;
    u->CreateOrCondition(typeIs(u, a).Get(), typeIs(u, b).Get(), &c);
    return c;
}

std::vector<Com<IUIAutomationElement>> list(IUIAutomationElementArray* arr) {
    std::vector<Com<IUIAutomationElement>> out;
    int n = 0;
    if (!arr || FAILED(arr->get_Length(&n))) return out;
    for (int i = 0; i < n; ++i) {
        Com<IUIAutomationElement> e;
        if (SUCCEEDED(arr->GetElement(i, &e)) && e) out.push_back(e);
    }
    return out;
}

std::vector<Com<IUIAutomationElement>> children(IUIAutomation* u, IUIAutomationElement* e, IUIAutomationCondition* c) {
    Com<IUIAutomationElementArray> arr;
    if (!e || !c || FAILED(e->FindAll(TreeScope_Children, c, &arr))) return {};
    return list(arr.Get());
}

std::wstring bstr(BSTR b) {
    std::wstring s = b ? std::wstring(b, SysStringLen(b)) : std::wstring();
    SysFreeString(b);
    return s;
}

CONTROLTYPEID typeOf(IUIAutomationElement* e) {
    CONTROLTYPEID t = 0;
    e->get_CurrentControlType(&t);
    return t;
}

Com<IUIAutomationExpandCollapsePattern> expander(IUIAutomationElement* e) {
    Com<IUIAutomationExpandCollapsePattern> p;
    e->GetCurrentPatternAs(UIA_ExpandCollapsePatternId, IID_PPV_ARGS(&p));
    return p;
}

bool opensSubmenu(IUIAutomationElement* e) {
    auto p = expander(e);
    ExpandCollapseState s = ExpandCollapseState_LeafNode;
    return p && SUCCEEDED(p->get_CurrentExpandCollapseState(&s)) && s != ExpandCollapseState_LeafNode;
}

// Nom d'une entrée : certains fournisseurs y laissent le raccourci après une tabulation.
std::pair<std::wstring, std::wstring> nameOf(IUIAutomationElement* e) {
    BSTR b = nullptr;
    e->get_CurrentName(&b);
    std::wstring name = bstr(b);
    std::wstring shortcut;
    if (auto tab = name.find(L'\t'); tab != std::wstring::npos) {
        shortcut = name.substr(tab + 1);
        name.resize(tab);
    }
    return {name, shortcut};
}

constexpr int kMenuBarDepth = 4;   // la barre de menus est près de la racine (Win32 : 1, WinUI : 2 à 4)
constexpr int kPopupDepth = 5;
constexpr int kMaxVisited = 400;

// Premier élément qui satisfait cond, à au plus depth niveaux sous scope, en largeur : jamais tout l'arbre (un
// WebView2 ou un document Office immense y seraient parcourus).
Com<IUIAutomationElement> findNear(IUIAutomation* u, IUIAutomationElement* scope, IUIAutomationCondition* cond, int depth) {
    if (!scope || !cond) return nullptr;
    Com<IUIAutomationTreeWalker> walker;
    if (FAILED(u->get_ControlViewWalker(&walker))) return nullptr;
    std::vector<Com<IUIAutomationElement>> level{scope};
    int visited = 0;
    for (int d = 0; d < depth && !level.empty(); ++d) {
        std::vector<Com<IUIAutomationElement>> next;
        for (const auto& parent : level) {
            Com<IUIAutomationElement> child;
            walker->GetFirstChildElement(parent.Get(), &child);
            while (child && visited++ < kMaxVisited) {
                Com<IUIAutomationElement> cached;
                // FindFirst(TreeScope_Element) : la condition sur cet élément seul.
                if (SUCCEEDED(child->FindFirst(TreeScope_Element, cond, &cached)) && cached) return cached;
                next.push_back(child);
                Com<IUIAutomationElement> sibling;
                walker->GetNextSiblingElement(child.Get(), &sibling);
                child = sibling;
            }
        }
        level = std::move(next);
    }
    return nullptr;
}

struct PidWindows {
    DWORD pid;
    HWND except;
    std::vector<HWND> out;
};

BOOL CALLBACK collectPidWindows(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<PidWindows*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == c->pid && h != c->except && IsWindowVisible(h)) c->out.push_back(h);
    return TRUE;
}

} // namespace

bool shouldProbeUia(std::wstring_view cls) {
    if (cls.empty()) return false;
    if (startsWith(cls, L"Chrome_WidgetWin_") || startsWith(cls, L"Mozilla")) return false;
    for (const wchar_t* shell : {L"Progman", L"WorkerW", L"CabinetWClass", L"ExploreWClass", L"Shell_TrayWnd"})
        if (cls == shell) return false;
    return true;
}

bool shouldProbeUia(std::wstring_view cls, const std::vector<std::wstring>& childClasses) {
    if (!shouldProbeUia(cls)) return false;
    for (const auto& c : childClasses)
        if (startsWith(c, L"Chrome_") || startsWith(c, L"Mozilla") || startsWith(c, L"Cef")) return false;
    return true;
}

bool UiaWorker::busy() const {
    std::lock_guard lock(mutex_);
    return running_ || !jobs_.empty();
}

UiaMenus::~UiaMenus() {
    if (uia_) uia_->Release();
}

bool UiaMenus::init() {
    Com<IUIAutomation> u;
    if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&u))) &&
        FAILED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&u))))
        return false;
    Com<IUIAutomation2> u2;
    if (SUCCEEDED(u.As(&u2))) {
        u2->put_ConnectionTimeout(1000);
        u2->put_TransactionTimeout(1500);
    }
    uia_ = u.Detach();
    return true;
}

IUIAutomationElement* UiaMenus::menuBar(HWND window) {
    Com<IUIAutomationElement> root;
    if (!uia_ || !window || FAILED(uia_->ElementFromHandle(window, &root)) || !root) return nullptr;
    VARIANT id{};
    id.vt = VT_BSTR;
    id.bstrVal = SysAllocString(L"SystemMenuBar");
    Com<IUIAutomationCondition> isSystem, notSystem, cond;
    uia_->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &isSystem);
    VariantClear(&id);
    if (!isSystem || FAILED(uia_->CreateNotCondition(isSystem.Get(), &notSystem)) ||
        FAILED(uia_->CreateAndCondition(typeIs(uia_, UIA_MenuBarControlTypeId).Get(), notSystem.Get(), &cond)))
        return nullptr;
    auto found = findNear(uia_, root.Get(), cond.Get(), kMenuBarDepth);
    return found.Detach();
}

namespace {

// Menu déplié sous cette entrée : ses entrées sont soit ses enfants, soit celles d'un élément Menu (enfant de
// l'entrée, fenêtre de menu de l'app sur le bureau, ou fenêtre surgissante dans l'arbre de la fenêtre). parent :
// menu déjà ouvert qui contient l'entrée (sous-menu), jamais pris pour le sien.
Com<IUIAutomationElement> openedMenu(IUIAutomation* u, IUIAutomationElement* item, HWND window,
                                     IUIAutomationElement* parent = nullptr) {
    auto entryCond = either(u, UIA_MenuItemControlTypeId, UIA_SeparatorControlTypeId);
    auto menuCond = typeIs(u, UIA_MenuControlTypeId);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    Com<IUIAutomationElement> root;
    u->ElementFromHandle(window, &root);
    auto other = [&](IUIAutomationElement* e) {
        BOOL same = FALSE;
        return !parent || FAILED(u->CompareElements(e, parent, &same)) || !same;
    };
    auto pick = [&](IUIAutomationElement* scope, TreeScope where, IUIAutomationCondition* c) -> Com<IUIAutomationElement> {
        if (!scope || !c) return nullptr;
        Com<IUIAutomationElementArray> arr;
        if (FAILED(scope->FindAll(where, c, &arr))) return nullptr;
        for (const auto& e : list(arr.Get()))
            if (other(e.Get())) return e;
        return nullptr;
    };
    // Fenêtres surgissantes de l'app (menus Win32 #32768, Qt, WinUI fenêtrées), la plus haute d'abord : un sous-menu
    // s'ouvre au-dessus du menu qui le contient.
    auto popup = [&]() -> Com<IUIAutomationElement> {
        PidWindows w{pid, window, {}};
        EnumWindows(collectPidWindows, reinterpret_cast<LPARAM>(&w));
        for (HWND h : w.out) {
            Com<IUIAutomationElement> e;
            if (FAILED(u->ElementFromHandle(h, &e)) || !e) continue;
            if (typeOf(e.Get()) == UIA_MenuControlTypeId && other(e.Get())) return e;
            if (auto m = pick(e.Get(), TreeScope_Children, menuCond.Get())) return m;
        }
        return nullptr;
    };
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        if (!children(u, item, entryCond.Get()).empty()) return item;
        if (auto m = pick(item, TreeScope_Children, menuCond.Get())) return m;
        if (auto m = popup()) return m;
        if (auto m = findNear(u, root.Get(), menuCond.Get(), kPopupDepth); m && other(m.Get())) return m;
        if (GetTickCount64() - start > kPopupWaitMs) return nullptr;
        Sleep(40);
    }
}

std::vector<Com<IUIAutomationElement>> entriesOf(IUIAutomation* u, IUIAutomationElement* menu) {
    // Vue brute : les séparateurs WinUI (Bloc-notes…) ne sont pas des « éléments de contrôle » et la vue par défaut les
    // écarte. Repli sur la vue par défaut si la demande échoue.
    auto cond = either(u, UIA_MenuItemControlTypeId, UIA_SeparatorControlTypeId);
    Com<IUIAutomationCacheRequest> req;
    Com<IUIAutomationCondition> raw;
    Com<IUIAutomationElementArray> arr;
    if (menu && cond && SUCCEEDED(u->CreateCacheRequest(&req)) && SUCCEEDED(u->CreateTrueCondition(&raw)) &&
        SUCCEEDED(req->put_TreeFilter(raw.Get())) && SUCCEEDED(req->put_AutomationElementMode(AutomationElementMode_Full)) &&
        SUCCEEDED(menu->FindAllBuildCache(TreeScope_Children, cond.Get(), req.Get(), &arr)) && arr) {
        auto raw = list(arr.Get());
        // Un conteneur non « contrôle » peut placer les entrées plus bas dans la vue brute : on garde la plus complète.
        auto plain = children(u, menu, cond.Get());
        return raw.size() >= plain.size() ? raw : plain;
    }
    return children(u, menu, cond.Get());
}

std::vector<RawMenuItem> readEntries(IUIAutomation* u, IUIAutomationElement* menu, HWND window, int depth, ULONGLONG start) {
    std::vector<RawMenuItem> out;
    const auto entries = entriesOf(u, menu);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        IUIAutomationElement* e = entries[i].Get();
        RawMenuItem it;
        it.position = int(i);
        if (typeOf(e) == UIA_SeparatorControlTypeId) {
            if (!out.empty() && !out.back().separator) {
                it.separator = true;
                out.push_back(std::move(it));
            }
            continue;
        }
        auto [name, shortcut] = nameOf(e);
        if (name.empty()) continue;
        it.text = std::move(name);
        BSTR key = nullptr;
        e->get_CurrentAcceleratorKey(&key);
        it.shortcut = bstr(key);
        if (it.shortcut.empty()) it.shortcut = std::move(shortcut);
        BOOL enabled = TRUE;
        e->get_CurrentIsEnabled(&enabled);
        it.enabled = enabled != FALSE;
        Com<IUIAutomationTogglePattern> toggle;
        ToggleState state = ToggleState_Off;
        if (SUCCEEDED(e->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle &&
            SUCCEEDED(toggle->get_CurrentToggleState(&state)))
            it.checked = state == ToggleState_On;
        it.popup = opensSubmenu(e);
        // Sous-menu : lu en le dépliant aussi, tant que le budget le permet ; sinon il se dépliera dans l'app.
        if (it.popup && it.enabled && depth > 0 && GetTickCount64() - start < kReadBudgetMs) {
            auto p = expander(e);
            if (p && SUCCEEDED(p->Expand())) {
                if (auto sub = openedMenu(u, e, window, menu))
                    it.children = readEntries(u, sub.Get(), window, depth - 1, start);
                p->Collapse();
            }
        }
        out.push_back(std::move(it));
    }
    if (!out.empty() && out.back().separator) out.pop_back();
    return out;
}

} // namespace

std::vector<RawMenuItem> UiaMenus::titles(HWND window) {
    Com<IUIAutomationElement> bar;
    bar.Attach(menuBar(window));
    std::vector<RawMenuItem> out;
    if (!bar) return out;
    const auto items = children(uia_, bar.Get(), typeIs(uia_, UIA_MenuItemControlTypeId).Get());
    for (std::size_t i = 0; i < items.size(); ++i) {
        auto [name, shortcut] = nameOf(items[i].Get());
        if (name.empty()) continue;
        RawMenuItem it;
        it.text = std::move(name);
        it.position = int(i);
        it.popup = true;
        BOOL enabled = TRUE;
        items[i]->get_CurrentIsEnabled(&enabled);
        it.enabled = enabled != FALSE;
        out.push_back(std::move(it));
    }
    return out;
}

std::optional<std::vector<RawMenuItem>> UiaMenus::items(HWND window, int title) {
    Com<IUIAutomationElement> bar;
    bar.Attach(menuBar(window));
    if (!bar) return std::nullopt;
    const auto titles = children(uia_, bar.Get(), typeIs(uia_, UIA_MenuItemControlTypeId).Get());
    if (title < 0 || std::size_t(title) >= titles.size()) return std::nullopt;
    IUIAutomationElement* item = titles[std::size_t(title)].Get();
    auto p = expander(item);
    if (!p || FAILED(p->Expand())) return std::nullopt;
    std::optional<std::vector<RawMenuItem>> out;
    if (auto menu = openedMenu(uia_, item, window)) out = readEntries(uia_, menu.Get(), window, 1, GetTickCount64());
    p->Collapse();
    return out;
}

bool UiaMenus::invoke(HWND window, const std::vector<int>& path, const std::wstring& name) {
    Com<IUIAutomationElement> bar;
    bar.Attach(menuBar(window));
    if (!bar || path.empty()) return false;
    const auto titles = children(uia_, bar.Get(), typeIs(uia_, UIA_MenuItemControlTypeId).Get());
    if (path[0] < 0 || std::size_t(path[0]) >= titles.size()) return false;
    Com<IUIAutomationElement> el = titles[std::size_t(path[0])];
    auto rootExpander = expander(el.Get());
    Com<IUIAutomationElement> parentMenu;
    auto fail = [&] {
        if (rootExpander) rootExpander->Collapse();
        log::warn(L"Barre : entrée « %s » introuvable dans le menu de l'app", name.c_str());
        return false;
    };
    for (std::size_t level = 1; level < path.size(); ++level) {
        auto p = expander(el.Get());
        if (!p || FAILED(p->Expand())) return fail();
        auto menu = openedMenu(uia_, el.Get(), window, parentMenu.Get());
        if (!menu) return fail();
        parentMenu = menu;
        const auto entries = entriesOf(uia_, menu.Get());
        const bool last = level + 1 == path.size();
        const std::size_t at = std::size_t(path[level]);
        Com<IUIAutomationElement> next = at < entries.size() ? entries[at] : nullptr;
        if (last && (!next || nameOf(next.Get()).first != name)) {   // le menu a changé : l'entrée par son nom
            next = nullptr;
            for (const auto& e : entries)
                if (nameOf(e.Get()).first == name) next = e;
        }
        if (!next) return fail();
        el = next;
    }
    Com<IUIAutomationInvokePattern> inv;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&inv))) && inv) return SUCCEEDED(inv->Invoke());
    if (auto p = expander(el.Get())) return SUCCEEDED(p->Expand());   // sous-menu non lu : déplié dans l'app
    Com<IUIAutomationTogglePattern> toggle;
    if (SUCCEEDED(el->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))) && toggle)
        return SUCCEEDED(toggle->Toggle());
    return fail();
}

// ---- Fil de travail ----

bool UiaWorker::start() {
    if (thread_.joinable()) return ok_;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    stopping_ = false;
    thread_ = std::thread([this, ready] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        {
            UiaMenus uia;
            ok_ = SUCCEEDED(com) && uia.init();
            SetEvent(ready);
            if (ok_) {
                for (;;) {
                    Job job;
                    {
                        std::unique_lock lock(mutex_);
                        wake_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                        if (stopping_) break;
                        job = std::move(jobs_.front());
                        jobs_.pop_front();
                        running_ = true;
                    }
                    job(uia);
                    running_ = false;
                }
            }
        }
        if (SUCCEEDED(com)) CoUninitialize();
    });
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    if (!ok_) log::warn(L"Barre : UI Automation indisponible, menus génériques seulement");
    return ok_;
}

void UiaWorker::stop() {
    if (!thread_.joinable()) return;
    {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    wake_.notify_all();
    thread_.join();
}

void UiaWorker::post(Job job) {
    {
        std::lock_guard lock(mutex_);
        if (!ok_ || stopping_) return;
        jobs_.push_back(std::move(job));
    }
    wake_.notify_one();
}

bool UiaWorker::call(Job job, DWORD timeoutMs) {
    struct Signal {
        HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        std::atomic<bool> abandoned{false};
        ~Signal() { CloseHandle(event); }
    };
    auto done = std::make_shared<Signal>();
    {
        std::lock_guard lock(mutex_);
        if (!ok_ || stopping_) return false;
        jobs_.push_back([job = std::move(job), done](UiaMenus& uia) {
            if (!done->abandoned) job(uia);
            SetEvent(done->event);
        });
    }
    wake_.notify_one();
    const ULONGLONG start = GetTickCount64();
    for (;;) {
        const ULONGLONG elapsed = GetTickCount64() - start;
        if (elapsed >= timeoutMs) break;
        const DWORD r = MsgWaitForMultipleObjects(1, &done->event, FALSE, DWORD(timeoutMs - elapsed), QS_SENDMESSAGE);
        if (r == WAIT_OBJECT_0) return true;
        if (r != WAIT_OBJECT_0 + 1) break;
        MSG m;
        PeekMessageW(&m, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);   // traite les messages envoyés
    }
    done->abandoned = true;
    return WaitForSingleObject(done->event, 0) == WAIT_OBJECT_0;
}

} // namespace md
