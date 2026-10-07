// Barre de menus : vrais menus des apps (lecture des menus Win32, construction des menus de la barre).
#include <windows.h>

#include <thread>

#include "minitest.h"
#include "../src/menubar/app_menus.h"
#include "../src/menubar/bar_actions.h"
#include "../src/menubar/win32_menu.h"

namespace {

// Menu de test : Fichier (Nouveau, Ouvrir…, séparateur, Récents ▸ (a.txt), Quitter grisé), Affichage (Barre d'état cochée,
// entrée owner-draw).
struct TestMenu {
    HMENU bar = CreateMenu(), file = CreatePopupMenu(), recent = CreatePopupMenu(), view = CreatePopupMenu();
    TestMenu() {
        AppendMenuW(file, MF_STRING, 101, L"&Nouveau\tCtrl+N");
        AppendMenuW(file, MF_STRING, 102, L"&Ouvrir…\tCtrl+O");
        AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(recent, MF_STRING, 110, L"&1 a.txt");
        AppendMenuW(file, MF_POPUP, reinterpret_cast<UINT_PTR>(recent), L"Fichiers &récents");
        AppendMenuW(file, MF_STRING | MF_GRAYED, 103, L"&Quitter");
        AppendMenuW(view, MF_STRING | MF_CHECKED, 201, L"Barre d'é&tat");
        AppendMenuW(view, MF_OWNERDRAW, 202, nullptr);
        AppendMenuW(view, MF_STRING, 203, L"Tom && Jerry");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Fichier");
        AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(view), L"&Affichage");
    }
    ~TestMenu() { DestroyMenu(bar); }   // détruit aussi les sous-menus
};

md::BarContext appContext() {
    md::BarContext c;
    c.appName = L"Bloc-notes";
    c.windows = {{1, L"a.txt"}};
    c.activeWindow = 1;
    return c;
}

std::vector<md::RawMenuItem> twoMenus(const std::wstring& second) {
    md::RawMenuItem open{L"Ouvrir…", L"Ctrl+O", 102};
    md::RawMenuItem file{L"Fichier"};
    file.popup = true;
    file.children = {open};
    md::RawMenuItem other{second};
    other.popup = true;
    other.children = {md::RawMenuItem{L"Sommaire", L"", 301}};
    return {file, other};
}

const md::BarMenu* findMenu(const md::BarMenus& m, const std::wstring& title) {
    for (const auto& menu : m.menus)
        if (menu.title == title) return &menu;
    return nullptr;
}

} // namespace

TEST_CASE(menu_label_parses_mnemonic_and_shortcut) {
    auto l = md::parseMenuLabel(L"&Enregistrer\tCtrl+S");
    CHECK(l.text == L"Enregistrer");
    CHECK(l.shortcut == L"Ctrl+S");
    l = md::parseMenuLabel(L"Tom && Jerry");
    CHECK(l.text == L"Tom & Jerry");
    CHECK(l.shortcut.empty());
    l = md::parseMenuLabel(L"Rechercher (&F)...\t");
    CHECK(l.text == L"Rechercher (F)...");
    CHECK(l.shortcut.empty());
    l = md::parseMenuLabel(L"&");
    CHECK(l.text.empty());
}

TEST_CASE(win32_menu_reads_nested_and_states) {
    TestMenu t;
    auto items = md::readWin32Menu(t.file);
    REQUIRE(items.size() == 5);
    CHECK(items[0].text == L"Nouveau");
    CHECK(items[0].shortcut == L"Ctrl+N");
    CHECK_EQ(items[0].id, UINT(101));
    CHECK(items[2].separator);
    CHECK(items[3].text == L"Fichiers récents");
    CHECK(items[3].popup);
    REQUIRE(items[3].children.size() == 1);
    CHECK(items[3].children[0].text == L"1 a.txt");
    CHECK(!items[4].enabled);

    auto view = md::readWin32Menu(t.view);
    REQUIRE(view.size() == 2);   // l'entrée owner-draw est omise
    CHECK(view[0].checked);
    CHECK(view[1].text == L"Tom & Jerry");
    CHECK_EQ(view[1].position, 2);   // position réelle dans le menu, entrée omise comprise

    // Le menu change (l'app ajoute un fichier récent, grise Nouveau) : une nouvelle lecture le voit.
    AppendMenuW(t.recent, MF_STRING, 111, L"&2 b.txt");
    EnableMenuItem(t.file, 101, MF_BYCOMMAND | MF_GRAYED);
    items = md::readWin32Menu(t.file);
    CHECK(!items[0].enabled);
    CHECK_EQ(items[3].children.size(), std::size_t(2));
}

TEST_CASE(win32_menu_titles) {
    TestMenu t;
    auto titles = md::win32MenuTitles(t.bar);
    REQUIRE(titles.size() == 2);
    CHECK(titles[0].text == L"Fichier");
    CHECK(titles[1].text == L"Affichage");
    CHECK_EQ(titles[1].position, 1);
    CHECK(titles[0].children.empty());   // titres seulement
    CHECK(md::win32MenuTitles(nullptr).empty());
}

TEST_CASE(menus_real_win32_replace_generic) {
    auto c = appContext();
    c.source = md::MenuSource::Win32;
    c.menuOwner = 42;
    c.real = twoMenus(L"Aide");
    auto m = md::buildBarMenus(c);
    // logo, app, Fichier, Fenêtre (ajoutée avant l'Aide), Aide
    REQUIRE(m.menus.size() == 5);
    CHECK(m.menus[1].title == L"Bloc-notes");
    CHECK(m.menus[2].title == L"Fichier");
    CHECK(m.menus[3].title == L"Fenêtre");
    CHECK(m.menus[4].title == L"Aide");
    CHECK(findMenu(m, L"Édition") == nullptr);   // pas de menus génériques
    const auto& open = m.menus[2].model.items.at(0);
    CHECK(open.text == L"Ouvrir…");
    CHECK(open.shortcut == L"Ctrl+O");
    const auto& a = m.actions.at(open.id);
    CHECK(a.kind == md::ActionKind::MenuCommand);
    CHECK_EQ(a.command, 102);
    CHECK_EQ(a.window, std::uint64_t(42));
}

TEST_CASE(menus_real_adds_window_menu_once) {
    auto c = appContext();
    c.source = md::MenuSource::Win32;
    c.real = twoMenus(L"Window");   // l'app a déjà son menu Fenêtre
    auto m = md::buildBarMenus(c);
    CHECK(findMenu(m, L"Fenêtre") == nullptr);
    REQUIRE(m.menus.size() == 4);
    c.real = twoMenus(L"Outils");   // pas de menu d'aide : Fenêtre en dernier
    m = md::buildBarMenus(c);
    REQUIRE(m.menus.size() == 5);
    CHECK(m.menus[4].title == L"Fenêtre");
}

TEST_CASE(menus_real_popup_and_empty_items) {
    auto c = appContext();
    c.source = md::MenuSource::Win32;
    md::RawMenuItem recent{L"Fichiers récents"};
    recent.popup = true;
    recent.children = {md::RawMenuItem{L"1 a.txt", L"", 110}};
    md::RawMenuItem emptyPopup{L"Vide"};
    emptyPopup.popup = true;
    md::RawMenuItem file{L"Fichier"};
    file.popup = true;
    file.children = {recent, md::RawMenuItem{.separator = true}, emptyPopup};
    c.real = {file};
    auto m = md::buildBarMenus(c);
    const auto& items = m.menus[2].model.items;
    REQUIRE(items.size() == 3);
    REQUIRE(items[0].submenu.size() == 1);
    CHECK_EQ(m.actions.at(items[0].submenu[0].id).command, 110);
    CHECK(items[1].separator());
    CHECK(!items[2].enabled);   // sous-menu vide : grisé, pas d'action
}

namespace {
struct Recorder {
    static inline UINT lastCommand = 0;
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_COMMAND) lastCommand = LOWORD(wp);
        return DefWindowProcW(h, msg, wp, lp);
    }
};
} // namespace

TEST_CASE(bar_actions_menu_command_posts_wm_command) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = Recorder::proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MacDockTestMenuOwner";
    RegisterClassW(&wc);
    // Fenêtre cachée : l'action ne doit pas lui donner le premier plan (aucun vol de focus pendant les tests).
    HWND w = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, -3000, -3000, 100, 100, nullptr, nullptr,
                             wc.hInstance, nullptr);
    REQUIRE(w != nullptr);
    HWND before = GetForegroundWindow();
    md::MenuAction a{md::ActionKind::MenuCommand};
    a.command = 102;
    a.window = reinterpret_cast<std::uintptr_t>(w);
    md::ActionContext ctx;
    CHECK(md::runAction(a, ctx, {}));
    MSG msg;
    while (PeekMessageW(&msg, w, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
    CHECK_EQ(Recorder::lastCommand, UINT(102));
    CHECK(GetForegroundWindow() == before);
    DestroyWindow(w);
    CHECK(!md::runAction(a, ctx, {}));   // fenêtre disparue
}

namespace {
// Fenêtre d'app sur son propre fil (comme une autre app) : son menu Fichier gagne une entrée à chaque
// WM_INITMENUPOPUP ; busyMs > 0 : le fil dort avant de traiter ses messages (app occupée).
struct MenuApp {
    HWND hwnd = nullptr;
    HMENU bar = nullptr, file = nullptr;
    std::thread thread;
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    static LRESULT CALLBACK proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_INITMENUPOPUP) AppendMenuW(reinterpret_cast<HMENU>(wp), MF_STRING, 150, L"&Ajoutée");
        return DefWindowProcW(h, msg, wp, lp);
    }
    explicit MenuApp(DWORD busyMs = 0) {
        thread = std::thread([this, busyMs] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = proc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"MacDockTestMenuApp";
            RegisterClassW(&wc);
            bar = CreateMenu();
            file = CreatePopupMenu();
            AppendMenuW(file, MF_STRING, 101, L"&Nouveau");
            AppendMenuW(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&Fichier");
            hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, -3000, -3000, 200, 200,
                                   nullptr, bar, wc.hInstance, nullptr);
            SetEvent(ready);
            if (busyMs) Sleep(busyMs);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) DispatchMessageW(&msg);
            DestroyWindow(hwnd);
        });
        WaitForSingleObject(ready, 5000);
    }
    ~MenuApp() {
        PostThreadMessageW(GetThreadId(thread.native_handle()), WM_QUIT, 0, 0);
        thread.join();
        CloseHandle(ready);
    }
};
} // namespace

TEST_CASE(win32_refresh_lets_app_update_menu) {
    MenuApp app;
    REQUIRE(app.hwnd != nullptr);
    CHECK(md::refreshWin32Popup(app.hwnd, GetMenu(app.hwnd), 0));
    auto items = md::readWin32Menu(GetSubMenu(GetMenu(app.hwnd), 0));
    REQUIRE(items.size() == 2);
    CHECK(items[1].text == L"Ajoutée");
}

TEST_CASE(win32_refresh_bounded_for_busy_app) {
    MenuApp app(1500);
    REQUIRE(app.hwnd != nullptr);
    const ULONGLONG start = GetTickCount64();
    CHECK(!md::refreshWin32Popup(app.hwnd, app.bar, 0));
    CHECK(GetTickCount64() - start < 700);
}
