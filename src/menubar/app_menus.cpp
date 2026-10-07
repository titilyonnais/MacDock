#include "app_menus.h"

#include <cwctype>

namespace md {
namespace {

class Builder {
public:
    explicit Builder(BarMenus& out) : out_(out) {}

    BarMenu& menu(std::wstring title, bool bold = false, bool logo = false) {
        out_.menus.push_back({std::move(title), {}, bold, logo});
        return out_.menus.back();
    }

    // Entrée avec son action, pas encore placée dans un menu.
    MenuItem item(std::wstring text, MenuAction action, std::wstring shortcut = {}, bool enabled = true) {
        MenuItem it;
        it.id = next_++;
        it.text = std::move(text);
        it.shortcut = std::move(shortcut);
        it.enabled = enabled;
        if (action.kind != ActionKind::None) out_.actions[it.id] = std::move(action);
        return it;
    }

    MenuItem& add(BarMenu& m, std::wstring text, MenuAction action, std::wstring shortcut = {}, bool enabled = true) {
        m.model.items.push_back(item(std::move(text), std::move(action), std::move(shortcut), enabled));
        return m.model.items.back();
    }

    // Entrée qui envoie son propre raccourci.
    MenuItem& key(BarMenu& m, std::wstring text, const std::wstring& shortcut, bool enabled = true) {
        return add(m, std::move(text), {ActionKind::Shortcut, shortcut}, shortcut, enabled);
    }

    static void separator(BarMenu& m) { m.model.items.push_back({}); }

private:
    BarMenus& out_;
    int next_ = 100;
};

// Comme sur macOS : Applications, puis Documents, chaque section sous un intitulé grisé, puis Effacer le menu.
MenuItem recentMenu(Builder& b, const BarContext& c) {
    MenuItem r;
    r.text = L"Éléments récents";
    auto section = [&](const wchar_t* title, const std::vector<RecentEntry>& list, ActionKind kind) {
        r.submenu.push_back(b.item(title, {}, {}, false));
        for (const auto& e : list) {
            MenuItem it = b.item(e.name, {kind, e.target});
            it.icon = e.icon;
            r.submenu.push_back(std::move(it));
        }
        r.submenu.push_back({});
    };
    section(L"Applications", c.recentApps, ActionKind::LaunchApp);
    section(L"Documents", c.recentDocs, ActionKind::OpenUri);
    r.submenu.push_back(b.item(L"Effacer le menu", {ActionKind::ClearRecent}, {}, !c.recentApps.empty() || !c.recentDocs.empty()));
    return r;
}

void logoMenu(Builder& b, const BarContext& c) {
    BarMenu& m = b.menu(L"", false, true);
    b.add(m, L"À propos de ce PC", {ActionKind::OpenUri, L"ms-settings:about"});
    Builder::separator(m);
    b.add(m, L"Réglages système…", {ActionKind::OpenUri, L"ms-settings:"});
    b.add(m, L"Microsoft Store…", {ActionKind::OpenUri, L"ms-windows-store:"});
    Builder::separator(m);
    m.model.items.push_back(recentMenu(b, c));
    Builder::separator(m);
    b.key(m, L"Forcer à quitter…", L"Ctrl+Maj+Échap");
    Builder::separator(m);
    b.add(m, L"Suspendre", {ActionKind::Sleep});
    b.add(m, L"Redémarrer…", {ActionKind::Restart});
    b.add(m, L"Éteindre…", {ActionKind::Shutdown});
    Builder::separator(m);
    b.add(m, L"Verrouiller l'écran", {ActionKind::Lock}, L"Win+L");
    b.add(m, c.userName.empty() ? L"Fermer la session…" : L"Fermer la session de " + c.userName + L"…",
          {ActionKind::SignOut});
}

void appMenu(Builder& b, const BarContext& c) {
    BarMenu& m = b.menu(c.appName, true);
    b.add(m, L"À propos de " + c.appName, {ActionKind::AboutApp});
    Builder::separator(m);
    b.key(m, L"Réglages…", L"Ctrl+,", !c.explorer);
    if (c.explorer) {
        Builder::separator(m);
        b.add(m, L"Vider la Corbeille…", {ActionKind::EmptyTrash});
    }
    Builder::separator(m);
    b.add(m, L"Masquer " + c.appName, {ActionKind::HideApp});
    b.add(m, L"Masquer les autres", {ActionKind::HideOthers});
    b.add(m, L"Tout afficher", {ActionKind::ShowAll});
    if (!c.explorer) {   // comme le Finder, l'Explorateur ne se quitte pas
        Builder::separator(m);
        b.add(m, L"Quitter " + c.appName, {ActionKind::QuitApp});
    }
}

void editMenu(Builder& b) {
    BarMenu& m = b.menu(L"Édition");
    b.key(m, L"Annuler", L"Ctrl+Z");
    b.key(m, L"Rétablir", L"Ctrl+Y");
    Builder::separator(m);
    b.key(m, L"Couper", L"Ctrl+X");
    b.key(m, L"Copier", L"Ctrl+C");
    b.key(m, L"Coller", L"Ctrl+V");
    b.key(m, L"Tout sélectionner", L"Ctrl+A");
    Builder::separator(m);
    b.key(m, L"Rechercher…", L"Ctrl+F");
    Builder::separator(m);
    b.key(m, L"Emoji et symboles", L"Win+.");
}

void viewMenu(Builder& b) {
    BarMenu& m = b.menu(L"Présentation");
    b.key(m, L"Actualiser", L"F5");
    b.key(m, L"Plein écran", L"F11");
    Builder::separator(m);
    b.key(m, L"Zoom avant", L"Ctrl+Plus");
    b.key(m, L"Zoom arrière", L"Ctrl+Moins");
    b.key(m, L"Taille réelle", L"Ctrl+0");
}

void windowMenu(Builder& b, const BarContext& c) {
    BarMenu& m = b.menu(L"Fenêtre");
    const bool hasWindow = !c.desktop;
    b.add(m, L"Réduire", {ActionKind::Minimize}, {}, hasWindow);
    b.add(m, L"Zoom", {ActionKind::Zoom}, {}, hasWindow);
    Builder::separator(m);
    b.key(m, L"Placer à gauche de l'écran", L"Win+←", hasWindow);
    b.key(m, L"Placer à droite de l'écran", L"Win+→", hasWindow);
    Builder::separator(m);
    b.add(m, L"Tout ramener au premier plan", {ActionKind::BringAllToFront});
    if (c.windows.empty()) return;
    Builder::separator(m);
    for (const auto& [id, title] : c.windows) {
        MenuItem& it = b.add(m, title.empty() ? L"(sans titre)" : title, {ActionKind::ActivateWindow, {}, id});
        it.checked = id == c.activeWindow;
    }
}

void helpMenu(Builder& b, const BarContext& c) {
    BarMenu& m = b.menu(L"Aide");
    b.key(m, L"Aide sur " + c.appName, L"F1");
}

void genericMenus(Builder& b, const BarContext& c) {
    BarMenu& file = b.menu(L"Fichier");
    b.key(file, L"Nouvelle fenêtre", L"Ctrl+N");
    b.key(file, L"Nouvel onglet", L"Ctrl+T");
    b.key(file, L"Ouvrir…", L"Ctrl+O");
    Builder::separator(file);
    b.key(file, L"Fermer l'onglet", L"Ctrl+W");
    b.add(file, L"Fermer la fenêtre", {ActionKind::CloseWindow}, L"Alt+F4");
    Builder::separator(file);
    b.key(file, L"Enregistrer", L"Ctrl+S");
    b.key(file, L"Enregistrer sous…", L"Ctrl+Maj+S");
    Builder::separator(file);
    b.key(file, L"Imprimer…", L"Ctrl+P");
    editMenu(b);
    viewMenu(b);
    windowMenu(b, c);
    helpMenu(b, c);
}

void explorerMenus(Builder& b, const BarContext& c) {
    const bool inWindow = !c.desktop;
    BarMenu& file = b.menu(L"Fichier");
    b.add(file, L"Nouvelle fenêtre", {ActionKind::OpenUri, L"explorer.exe"}, L"Win+E");
    b.key(file, L"Nouveau dossier", L"Ctrl+Maj+N");
    Builder::separator(file);
    b.add(file, L"Fermer la fenêtre", {ActionKind::CloseWindow}, L"Alt+F4", inWindow);
    editMenu(b);
    viewMenu(b);
    BarMenu& go = b.menu(L"Aller");
    b.key(go, L"Précédent", L"Alt+←", inWindow);
    b.key(go, L"Suivant", L"Alt+→", inWindow);
    b.key(go, L"Dossier parent", L"Alt+↑", inWindow);
    Builder::separator(go);
    const std::pair<const wchar_t*, const wchar_t*> places[] = {
        {L"Récents", L"shell:Recent"},
        {L"Documents", L"shell:Personal"},
        {L"Bureau", L"shell:Desktop"},
        {L"Téléchargements", L"shell:Downloads"},
        {L"Accueil", L"shell:::{f874310e-b6b7-47dc-bc84-b9e6b38f5903}"},
        {L"Ce PC", L"shell:MyComputerFolder"},
        {L"Réseau", L"shell:NetworkPlacesFolder"},
        {L"Applications", L"shell:AppsFolder"},
        {L"Corbeille", L"shell:RecycleBinFolder"},
    };
    for (const auto& [text, path] : places) b.add(go, text, {ActionKind::GoTo, path});
    windowMenu(b, c);
    helpMenu(b, c);
}

std::wstring lower(std::wstring s) {
    for (auto& ch : s) ch = wchar_t(std::towlower(ch));
    return s;
}

bool isWindowTitle(const std::wstring& t) {
    const std::wstring l = lower(t);
    return l == L"fenêtre" || l == L"fenetre" || l == L"window" || l == L"fenêtres" || l == L"windows";
}

bool isHelpTitle(const std::wstring& t) {
    const std::wstring l = lower(t);
    return l == L"aide" || l == L"help" || l == L"?";
}

MenuAction uiaAction(const RawMenuItem& r, const BarContext& c, std::vector<int> path) {
    MenuAction a{ActionKind::UiaInvoke, r.text, c.menuOwner};
    a.path = std::move(path);
    return a;
}

// path : positions depuis le titre (UI Automation retrouve l'entrée par ce chemin).
MenuItem realItem(Builder& b, const RawMenuItem& r, const BarContext& c, std::vector<int> path) {
    if (r.separator) return {};
    path.push_back(r.position);
    const bool uia = c.source == MenuSource::Uia;
    if (r.popup) {
        MenuItem it;
        if (!r.children.empty()) {
            it.text = r.text;
            it.enabled = r.enabled;
            for (const auto& child : r.children) it.submenu.push_back(realItem(b, child, c, path));
        } else if (uia) {
            it = b.item(r.text, uiaAction(r, c, path), {}, r.enabled);   // sous-menu non lu : déplié dans l'app
        } else {
            it = b.item(r.text, {}, {}, false);   // sous-menu vide : rien à montrer
        }
        return it;
    }
    MenuAction a = uia ? uiaAction(r, c, path) : MenuAction{ActionKind::MenuCommand, {}, c.menuOwner, int(r.id)};
    MenuItem it = b.item(r.text, std::move(a), r.shortcut, r.enabled);
    it.checked = r.checked;
    return it;
}

void realMenus(Builder& b, const BarContext& c) {
    bool hasWindow = false;
    for (const auto& title : c.real) hasWindow = hasWindow || isWindowTitle(title.text);
    // Comme sur macOS, Fenêtre se place juste avant l'Aide.
    const std::size_t helpAt = !c.real.empty() && isHelpTitle(c.real.back().text) ? c.real.size() - 1 : c.real.size();
    for (std::size_t i = 0; i < c.real.size(); ++i) {
        if (i == helpAt && !hasWindow) windowMenu(b, c);
        BarMenu& m = b.menu(c.real[i].text);
        m.real = int(i);
        for (const auto& child : c.real[i].children) m.model.items.push_back(realItem(b, child, c, {c.real[i].position}));
        if (m.model.items.empty()) b.add(m, L"Aucun élément", {}, {}, false);   // pas encore lu, ou illisible
    }
    if (helpAt == c.real.size() && !hasWindow) windowMenu(b, c);
}

} // namespace

BarMenus buildBarMenus(const BarContext& c) {
    BarMenus out;
    Builder b(out);
    logoMenu(b, c);
    appMenu(b, c);
    if (c.source != MenuSource::Generic && !c.real.empty()) realMenus(b, c);
    else if (c.explorer) explorerMenus(b, c);
    else genericMenus(b, c);
    return out;
}

} // namespace md
