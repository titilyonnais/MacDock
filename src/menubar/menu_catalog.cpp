#include "menu_catalog.h"

#include <cwctype>

namespace md {
namespace {

CatalogItem K(std::wstring text, std::wstring shortcut) { return {std::move(text), std::move(shortcut), {}, {}}; }
CatalogItem U(std::wstring text, std::wstring uri) { return {std::move(text), {}, std::move(uri), {}}; }
CatalogItem Sep() { return {}; }
CatalogItem Sub(std::wstring text, std::vector<CatalogItem> items) { return {std::move(text), {}, {}, std::move(items)}; }

// Édition commune : presse-papiers, recherche, emoji.
std::vector<CatalogItem> editItems(const wchar_t* redo, std::vector<CatalogItem> search) {
    std::vector<CatalogItem> v{K(L"Annuler", L"Ctrl+Z"), K(L"Rétablir", redo), Sep(),
                               K(L"Couper", L"Ctrl+X"), K(L"Copier", L"Ctrl+C"), K(L"Coller", L"Ctrl+V"),
                               K(L"Tout sélectionner", L"Ctrl+A")};
    if (!search.empty()) {
        v.push_back(Sep());
        for (auto& s : search) v.push_back(std::move(s));
    }
    v.push_back(Sep());
    v.push_back(K(L"Emoji et symboles", L"Win+."));
    return v;
}

std::vector<CatalogItem> zoomItems() {
    return {K(L"Taille réelle", L"Ctrl+0"), K(L"Zoom avant", L"Ctrl+Plus"), K(L"Zoom arrière", L"Ctrl+Moins")};
}

// Applis Electron : actualiser, zoom, outils de développement (communs à Chromium).
std::vector<CatalogItem> electronView(std::vector<CatalogItem> extra = {}) {
    std::vector<CatalogItem> v = std::move(extra);
    if (!v.empty()) v.push_back(Sep());
    v.push_back(K(L"Actualiser", L"Ctrl+R"));
    v.push_back(Sep());
    for (auto& z : zoomItems()) v.push_back(std::move(z));
    v.push_back(Sep());
    v.push_back(K(L"Outils de développement", L"Ctrl+Maj+I"));
    return v;
}

std::vector<MenuCatalog> build() {
    std::vector<MenuCatalog> all;

    // Navigateurs Chromium : menus de Chrome sur macOS (Fichier, Édition, Présentation, Historique, Favoris, Onglet).
    {
        MenuCatalog a;
        a.exes = {L"chrome.exe", L"brave.exe", L"msedge.exe", L"vivaldi.exe", L"opera.exe", L"chromium.exe", L"thorium.exe"};
        a.settingsShortcut.clear();   // pas de raccourci : Réglages ouverts depuis le menu de l'app (absent ici)
        a.menus.push_back({L"Fichier",
                           {K(L"Nouvel onglet", L"Ctrl+T"), K(L"Nouvelle fenêtre", L"Ctrl+N"),
                            K(L"Nouvelle fenêtre de navigation privée", L"Ctrl+Maj+N"),
                            K(L"Rouvrir l'onglet fermé", L"Ctrl+Maj+T"), K(L"Ouvrir le fichier…", L"Ctrl+O"),
                            K(L"Ouvrir l'adresse…", L"Ctrl+L"), Sep(), K(L"Fermer la fenêtre", L"Ctrl+Maj+W"),
                            K(L"Fermer l'onglet", L"Ctrl+W"), K(L"Enregistrer la page sous…", L"Ctrl+S"), Sep(),
                            K(L"Imprimer…", L"Ctrl+P")}});
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Maj+Z", {Sub(L"Rechercher", {K(L"Rechercher…", L"Ctrl+F"),
                                                                                       K(L"Rechercher le suivant", L"Ctrl+G"),
                                                                                       K(L"Rechercher le précédent", L"Ctrl+Maj+G")}),
                                                                   K(L"Coller et adapter le style", L"Ctrl+Maj+V")})});
        a.menus.push_back({L"Présentation",
                           {K(L"Toujours afficher la barre de favoris", L"Ctrl+Maj+B"), Sep(), K(L"Arrêter", L"Échap"),
                            K(L"Actualiser cette page", L"Ctrl+R"), K(L"Forcer l'actualisation", L"Ctrl+Maj+R"), Sep(),
                            K(L"Plein écran", L"F11"), K(L"Taille réelle", L"Ctrl+0"), K(L"Zoom avant", L"Ctrl+Plus"),
                            K(L"Zoom arrière", L"Ctrl+Moins"), Sep(),
                            Sub(L"Développeur", {K(L"Afficher la source", L"Ctrl+U"),
                                                  K(L"Outils de développement", L"Ctrl+Maj+I"),
                                                  K(L"Console JavaScript", L"Ctrl+Maj+J")})}});
        a.menus.push_back({L"Historique",
                           {K(L"Accueil", L"Alt+Origine"), K(L"Précédent", L"Alt+←"), K(L"Suivant", L"Alt+→"), Sep(),
                            K(L"Afficher tout l'historique", L"Ctrl+H"),
                            K(L"Effacer les données de navigation…", L"Ctrl+Maj+Suppr")}});
        a.menus.push_back({L"Favoris",
                           {K(L"Gestionnaire de favoris", L"Ctrl+Maj+O"), K(L"Ajouter cette page aux favoris…", L"Ctrl+D"),
                            K(L"Ajouter tous les onglets aux favoris…", L"Ctrl+Maj+D")}});
        a.menus.push_back({L"Onglet",
                           {K(L"Onglet suivant", L"Ctrl+Tab"), K(L"Onglet précédent", L"Ctrl+Maj+Tab"), Sep(),
                            K(L"Rechercher dans les onglets…", L"Ctrl+Maj+A")}});
        a.menus.push_back({L"Aide", {K(L"Centre d'aide", L"F1")}});
        a.windowExtras = {K(L"Téléchargements", L"Ctrl+J"), K(L"Gestionnaire de tâches", L"Maj+Échap")};
        all.push_back(std::move(a));
    }

    // Firefox : menus de Firefox sur macOS.
    {
        MenuCatalog a;
        a.exes = {L"firefox.exe", L"librewolf.exe", L"floorp.exe", L"zen.exe", L"waterfox.exe"};
        a.settingsShortcut.clear();
        a.menus.push_back({L"Fichier",
                           {K(L"Nouvel onglet", L"Ctrl+T"), K(L"Nouvelle fenêtre", L"Ctrl+N"),
                            K(L"Nouvelle fenêtre privée", L"Ctrl+Maj+P"), K(L"Ouvrir un fichier…", L"Ctrl+O"), Sep(),
                            K(L"Enregistrer sous…", L"Ctrl+S"), Sep(), K(L"Imprimer…", L"Ctrl+P"), Sep(),
                            K(L"Fermer l'onglet", L"Ctrl+W"), K(L"Fermer la fenêtre", L"Ctrl+Maj+W")}});
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Maj+Z", {K(L"Rechercher dans la page…", L"Ctrl+F"),
                                                                   K(L"Rechercher à nouveau", L"Ctrl+G")})});
        a.menus.push_back({L"Affichage",
                           {Sub(L"Barre latérale", {K(L"Marque-pages", L"Ctrl+B"), K(L"Historique", L"Ctrl+H")}), Sep(),
                            K(L"Zoom avant", L"Ctrl+Plus"), K(L"Zoom arrière", L"Ctrl+Moins"),
                            K(L"Réinitialiser le zoom", L"Ctrl+0"), Sep(), K(L"Mode lecture", L"F9"),
                            K(L"Plein écran", L"F11")}});
        a.menus.push_back({L"Historique",
                           {K(L"Précédent", L"Alt+←"), K(L"Suivant", L"Alt+→"), K(L"Accueil", L"Alt+Origine"), Sep(),
                            K(L"Afficher tout l'historique", L"Ctrl+Maj+H"),
                            K(L"Effacer l'historique récent…", L"Ctrl+Maj+Suppr"), Sep(),
                            K(L"Rouvrir l'onglet fermé", L"Ctrl+Maj+T")}});
        a.menus.push_back({L"Marque-pages",
                           {K(L"Gérer les marque-pages", L"Ctrl+Maj+O"), K(L"Marquer cet onglet…", L"Ctrl+D"),
                            K(L"Marquer tous les onglets…", L"Ctrl+Maj+D")}});
        a.menus.push_back({L"Outils",
                           {K(L"Téléchargements", L"Ctrl+Maj+Y"), K(L"Modules et thèmes", L"Ctrl+Maj+A"), Sep(),
                            Sub(L"Outils de navigation", {K(L"Outils de développement", L"Ctrl+Maj+I"),
                                                           K(L"Console du navigateur", L"Ctrl+Maj+K"),
                                                           K(L"Code source de la page", L"Ctrl+U")})}});
        a.menus.push_back({L"Aide", {K(L"Aide de Firefox", L"F1")}});
        all.push_back(std::move(a));
    }

    // Terminal : menus du Terminal de macOS (Shell, Édition, Présentation).
    {
        MenuCatalog a;
        a.exes = {L"windowsterminal.exe", L"wt.exe"};
        a.menus.push_back({L"Shell",
                           {K(L"Nouvel onglet", L"Ctrl+Maj+T"), K(L"Nouvelle fenêtre", L"Ctrl+Maj+N"),
                            K(L"Dupliquer l'onglet", L"Ctrl+Maj+D"), Sep(),
                            K(L"Diviser verticalement", L"Alt+Maj+Plus"), K(L"Diviser horizontalement", L"Alt+Maj+Moins"),
                            Sep(), K(L"Fermer", L"Ctrl+Maj+W")}});
        a.menus.push_back({L"Édition",
                           {K(L"Copier", L"Ctrl+Maj+C"), K(L"Coller", L"Ctrl+Maj+V"), K(L"Tout sélectionner", L"Ctrl+Maj+A"),
                            Sep(), K(L"Rechercher…", L"Ctrl+Maj+F"), Sep(), K(L"Emoji et symboles", L"Win+.")}});
        a.menus.push_back({L"Présentation",
                           {K(L"Palette de commandes…", L"Ctrl+Maj+P"), Sep(), K(L"Plein écran", L"F11"), Sep(),
                            K(L"Taille réelle", L"Ctrl+0"), K(L"Agrandir le texte", L"Ctrl+Plus"),
                            K(L"Réduire le texte", L"Ctrl+Moins")}});
        all.push_back(std::move(a));
    }

    // Visual Studio Code : ses menus sur macOS.
    {
        MenuCatalog a;
        a.exes = {L"code.exe", L"code - insiders.exe", L"cursor.exe", L"windsurf.exe", L"vscodium.exe"};
        a.menus.push_back({L"Fichier",
                           {K(L"Nouveau fichier texte", L"Ctrl+N"), K(L"Nouvelle fenêtre", L"Ctrl+Maj+N"), Sep(),
                            K(L"Ouvrir le fichier…", L"Ctrl+O"), Sep(), K(L"Enregistrer", L"Ctrl+S"),
                            K(L"Enregistrer sous…", L"Ctrl+Maj+S"), Sep(), K(L"Fermer l'éditeur", L"Ctrl+F4")}});
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Y", {K(L"Rechercher", L"Ctrl+F"), K(L"Remplacer", L"Ctrl+H"), Sep(),
                                                               K(L"Rechercher dans les fichiers", L"Ctrl+Maj+F"),
                                                               K(L"Remplacer dans les fichiers", L"Ctrl+Maj+H")})});
        a.menus.push_back({L"Sélection",
                           {K(L"Tout sélectionner", L"Ctrl+A"), K(L"Développer la sélection", L"Maj+Alt+→"),
                            K(L"Réduire la sélection", L"Maj+Alt+←"), Sep(),
                            K(L"Copier la ligne vers le haut", L"Maj+Alt+↑"), K(L"Copier la ligne vers le bas", L"Maj+Alt+↓"),
                            K(L"Déplacer la ligne vers le haut", L"Alt+↑"), K(L"Déplacer la ligne vers le bas", L"Alt+↓")}});
        a.menus.push_back({L"Présentation",
                           {K(L"Palette de commandes…", L"Ctrl+Maj+P"), Sep(), K(L"Explorateur", L"Ctrl+Maj+E"),
                            K(L"Recherche", L"Ctrl+Maj+F"), K(L"Contrôle de code source", L"Ctrl+Maj+G"),
                            K(L"Exécuter et déboguer", L"Ctrl+Maj+D"), K(L"Extensions", L"Ctrl+Maj+X"), Sep(),
                            K(L"Problèmes", L"Ctrl+Maj+M"), Sep(), K(L"Plein écran", L"F11"),
                            K(L"Barre latérale", L"Ctrl+B"), K(L"Zoom avant", L"Ctrl+Plus"),
                            K(L"Zoom arrière", L"Ctrl+Moins")}});
        a.menus.push_back({L"Atteindre",
                           {K(L"Précédent", L"Alt+←"), K(L"Suivant", L"Alt+→"), Sep(), K(L"Atteindre le fichier…", L"Ctrl+P"),
                            K(L"Atteindre le symbole…", L"Ctrl+Maj+O"), K(L"Atteindre la ligne…", L"Ctrl+G"),
                            K(L"Atteindre la définition", L"F12"), Sep(), K(L"Problème suivant", L"F8")}});
        a.menus.push_back({L"Exécuter",
                           {K(L"Démarrer le débogage", L"F5"), K(L"Exécuter sans débogage", L"Ctrl+F5"),
                            K(L"Arrêter le débogage", L"Maj+F5"), K(L"Redémarrer le débogage", L"Ctrl+Maj+F5"), Sep(),
                            K(L"Pas à pas principal", L"F10"), K(L"Pas à pas détaillé", L"F11"), Sep(),
                            K(L"Basculer le point d'arrêt", L"F9")}});
        a.menus.push_back({L"Terminal", {K(L"Exécuter la tâche de build…", L"Ctrl+Maj+B")}});
        all.push_back(std::move(a));
    }

    // Discord : menus de Discord sur macOS, plus la navigation et l'appel (raccourcis de Discord).
    {
        MenuCatalog a;
        a.exes = {L"discord.exe", L"discordptb.exe", L"discordcanary.exe"};
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Y", {K(L"Rechercher", L"Ctrl+F")})});
        a.menus.push_back({L"Présentation", electronView()});
        a.menus.push_back({L"Aller",
                           {K(L"Aller à…", L"Ctrl+K"), Sep(), K(L"Serveur précédent", L"Ctrl+Alt+↑"),
                            K(L"Serveur suivant", L"Ctrl+Alt+↓"), K(L"Salon précédent", L"Alt+↑"),
                            K(L"Salon suivant", L"Alt+↓"), Sep(), K(L"Boîte de réception", L"Ctrl+I")}});
        a.menus.push_back({L"Appel",
                           {K(L"Couper le micro", L"Ctrl+Maj+M"), K(L"Mettre en sourdine", L"Ctrl+Maj+D"), Sep(),
                            K(L"Répondre", L"Ctrl+Entrée"), K(L"Refuser", L"Échap")}});
        all.push_back(std::move(a));
    }

    // Telegram Desktop.
    {
        MenuCatalog a;
        a.exes = {L"telegram.exe", L"ayugram.exe", L"kotatogram.exe"};
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Maj+Z", {K(L"Rechercher", L"Ctrl+F")})});
        a.menus.push_back({L"Discussions",
                           {K(L"Messages enregistrés", L"Ctrl+0"), Sep(), K(L"Discussion suivante", L"Ctrl+Tab"),
                            K(L"Discussion précédente", L"Ctrl+Maj+Tab"), Sep(), K(L"Verrouiller Telegram", L"Ctrl+L")}});
        all.push_back(std::move(a));
    }

    // Notion.
    {
        MenuCatalog a;
        a.exes = {L"notion.exe", L"notion calendar.exe"};
        a.menus.push_back({L"Fichier",
                           {K(L"Nouvelle page", L"Ctrl+N"), K(L"Nouvelle fenêtre", L"Ctrl+Maj+N"), K(L"Nouvel onglet", L"Ctrl+T"),
                            Sep(), K(L"Rouvrir l'onglet fermé", L"Ctrl+Maj+T"), K(L"Fermer l'onglet", L"Ctrl+W")}});
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Maj+Z", {K(L"Rechercher…", L"Ctrl+P"),
                                                                   K(L"Copier le lien de la page", L"Ctrl+L")})});
        a.menus.push_back({L"Présentation", electronView({K(L"Basculer le mode sombre", L"Ctrl+Maj+L")})});
        all.push_back(std::move(a));
    }

    // Spotify : menu Lecture de Spotify sur macOS.
    {
        MenuCatalog a;
        a.exes = {L"spotify.exe"};
        a.menus.push_back({L"Fichier", {K(L"Nouvelle playlist", L"Ctrl+N")}});
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Y", {})});
        a.menus.push_back({L"Lecture",
                           {K(L"Lecture/Pause", L"Espace"), K(L"Suivant", L"Ctrl+→"), K(L"Précédent", L"Ctrl+←"), Sep(),
                            K(L"Monter le volume", L"Ctrl+↑"), K(L"Baisser le volume", L"Ctrl+↓"), Sep(),
                            K(L"Lecture aléatoire", L"Ctrl+S"), K(L"Répétition", L"Ctrl+R")}});
        all.push_back(std::move(a));
    }

    // Steam : menus de Steam sur macOS, par ses adresses steam://.
    {
        MenuCatalog a;
        a.exes = {L"steam.exe", L"steamwebhelper.exe"};
        a.settingsShortcut.clear();
        a.settingsUri = L"steam://open/settings";
        a.menus.push_back({L"Affichage",
                           {U(L"Magasin", L"steam://store"), U(L"Bibliothèque", L"steam://open/games"),
                            U(L"Téléchargements", L"steam://open/downloads"), Sep(), U(L"Mode Big Picture", L"steam://open/bigpicture"),
                            Sep(), U(L"Console", L"steam://open/console")}});
        a.menus.push_back({L"Amis", {U(L"Liste d'amis", L"steam://open/friends")}});
        all.push_back(std::move(a));
    }

    // Applis Electron courantes sans menus propres : Édition et Présentation d'Electron.
    {
        MenuCatalog a;
        a.exes = {L"claude.exe", L"slack.exe", L"whatsapp.exe", L"signal.exe", L"obsidian.exe", L"figma.exe",
                  L"chatgpt.exe", L"teams.exe", L"ms-teams.exe", L"postman.exe", L"github desktop.exe", L"githubdesktop.exe"};
        a.menus.push_back({L"Édition", editItems(L"Ctrl+Maj+Z", {K(L"Rechercher", L"Ctrl+F")})});
        a.menus.push_back({L"Présentation", electronView()});
        all.push_back(std::move(a));
    }
    return all;
}

std::wstring lower(std::wstring_view s) {
    std::wstring l(s);
    for (auto& ch : l) ch = wchar_t(std::towlower(ch));
    return l;
}

} // namespace

const std::vector<MenuCatalog>& menuCatalog() {
    static const std::vector<MenuCatalog> catalog = build();
    return catalog;
}

const MenuCatalog* menuCatalogFor(std::wstring_view exeName) {
    if (exeName.empty()) return nullptr;
    const std::wstring name = lower(exeName);
    for (const auto& app : menuCatalog())
        for (const auto& exe : app.exes)
            if (exe == name) return &app;
    return nullptr;
}

} // namespace md
