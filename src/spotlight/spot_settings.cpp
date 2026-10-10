#include "spot_settings.h"

#include <algorithm>
#include <map>
#include <string_view>

#include "../apps/app_catalog.h"
#include "../core/strings.h"
#include "../settings/pane_catalog.h"

namespace md {

namespace {

// Mots-clés des sections de Réglages MacDock (clé de --pane) : ce que l'on y règle.
const std::map<std::string, std::wstring>& paneKeywords() {
    static const std::map<std::string, std::wstring> words = {
        {"general", L"macdock démarrage ouvrir à la connexion sauvegarde exporter importer apparence couleur d'accentuation accent"},
        {"dock", L"macdock taille agrandissement position masquer automatiquement"},
        {"menubar", L"macdock horloge icônes volume"},
        {"windows", L"macdock pastilles génie réduire"},
        {"desktop", L"macdock coins actifs exposé"},
        {"keyboard", L"macdock raccourcis touche commande fenêtre suivante"},
        {"screenshots", L"macdock capture enregistrement de l'écran vignette"},
        {"sounds", L"macdock effets sonores"},
        {"font", L"macdock sf pro"},
        {"mods", L"macdock windhawk"},
        {"about", L"macdock version mise à jour de logiciels"},
    };
    return words;
}

// Pages des Paramètres de Windows 11 (noms de l'interface française), par ordre d'intérêt : à égalité, la première.
struct WindowsPage {
    const wchar_t* title;
    const wchar_t* uri;
    const wchar_t* keywords;
};
constexpr WindowsPage kWindowsPages[] = {
    {L"Wi-Fi", L"ms-settings:network-wifi", L"wifi sans fil réseau internet connexion"},
    {L"Bluetooth", L"ms-settings:bluetooth", L"appareils casque écouteurs manette associer jumeler"},
    {L"Réseau et Internet", L"ms-settings:network-status", L"connexion état du réseau"},
    {L"Ethernet", L"ms-settings:network-ethernet", L"câble réseau filaire"},
    {L"VPN", L"ms-settings:network-vpn", L"réseau privé virtuel"},
    {L"Mode Avion", L"ms-settings:network-airplanemode", L"avion"},
    {L"Point d'accès sans fil mobile", L"ms-settings:network-mobilehotspot", L"partage de connexion hotspot"},
    {L"Proxy", L"ms-settings:network-proxy", L""},
    {L"Paramètres réseau avancés", L"ms-settings:network-advancedsettings", L"carte réseau adaptateur dns"},
    {L"Écran", L"ms-settings:display", L"affichage résolution luminosité mise à l'échelle hdr orientation moniteur"},
    {L"Éclairage nocturne", L"ms-settings:nightlight", L"veilleuse lumière bleue night shift"},
    {L"Son", L"ms-settings:sound", L"audio volume haut-parleurs casque micro microphone sortie entrée"},
    {L"Mélangeur de volume", L"ms-settings:apps-volume", L"volume des applications son"},
    {L"Notifications", L"ms-settings:notifications", L"alertes ne pas déranger"},
    {L"Concentration", L"ms-settings:quiethours", L"focus assistant de concentration"},
    {L"Alimentation et batterie", L"ms-settings:powersleep", L"énergie batterie veille économiseur mode d'alimentation"},
    {L"Stockage", L"ms-settings:storagesense", L"disque espace assistant de stockage nettoyage"},
    {L"Multitâche", L"ms-settings:multitasking", L"ancrer snap dispositions alt tab bureaux virtuels"},
    {L"Presse-papiers", L"ms-settings:clipboard", L"historique copier coller"},
    {L"Bureau à distance", L"ms-settings:remotedesktop", L"rdp accès à distance"},
    {L"Informations système", L"ms-settings:about",
     L"à propos nom du pc renommer processeur mémoire ram version de windows spécifications"},
    {L"Activation", L"ms-settings:activation", L"clé de produit licence"},
    {L"Récupération", L"ms-settings:recovery", L"réinitialiser ce pc démarrage avancé restauration"},
    {L"Résolution des problèmes", L"ms-settings:troubleshoot", L"dépannage utilitaires"},
    {L"Espace développeurs", L"ms-settings:developers", L"pour les développeurs mode développeur"},
    {L"Personnalisation", L"ms-settings:personalization", L"apparence"},
    {L"Arrière-plan", L"ms-settings:personalization-background", L"fond d'écran image papier peint diaporama"},
    {L"Couleurs", L"ms-settings:colors", L"mode sombre mode clair apparence couleur d'accentuation transparence"},
    {L"Thèmes", L"ms-settings:themes", L"thème curseurs icônes du bureau"},
    {L"Écran de verrouillage", L"ms-settings:lockscreen", L"verrouillage"},
    {L"Démarrer", L"ms-settings:personalization-start", L"menu démarrer"},
    {L"Barre des tâches", L"ms-settings:taskbar", L"icônes cachées zone de notification"},
    {L"Polices", L"ms-settings:fonts", L"police caractères typographie"},
    {L"Applications installées", L"ms-settings:appsfeatures",
     L"désinstaller programmes logiciels applications et fonctionnalités"},
    {L"Applications par défaut", L"ms-settings:defaultapps", L"navigateur par défaut ouvrir avec types de fichiers"},
    {L"Démarrage", L"ms-settings:startupapps", L"applications au démarrage lancement ouverture de session"},
    {L"Fonctionnalités facultatives", L"ms-settings:optionalfeatures", L"fonctionnalités windows"},
    {L"Vos informations", L"ms-settings:yourinfo", L"compte photo de profil compte microsoft"},
    {L"Options de connexion", L"ms-settings:signinoptions",
     L"mot de passe code pin windows hello empreinte visage verrouillage dynamique"},
    {L"E-mail et comptes", L"ms-settings:emailandaccounts", L"messagerie courrier comptes"},
    {L"Autres utilisateurs", L"ms-settings:otherusers", L"famille utilisateurs ajouter un compte"},
    {L"Sauvegarde Windows", L"ms-settings:backup", L"sauvegarde onedrive synchroniser"},
    {L"Date et heure", L"ms-settings:dateandtime", L"heure date fuseau horaire horloge"},
    {L"Langue et région", L"ms-settings:regionlanguage", L"langue région pays format"},
    {L"Clavier", L"ms-settings:keyboard", L"disposition azerty méthode de saisie"},
    {L"Saisie", L"ms-settings:typing", L"correction automatique suggestions de texte orthographe"},
    {L"Voix", L"ms-settings:speech", L"reconnaissance vocale"},
    {L"Game Bar", L"ms-settings:gaming-gamebar", L"xbox jeux"},
    {L"Mode jeu", L"ms-settings:gaming-gamemode", L"jeux"},
    {L"Taille du texte", L"ms-settings:easeofaccess-display", L"accessibilité texte plus grand agrandir"},
    {L"Effets visuels", L"ms-settings:easeofaccess-visualeffects",
     L"accessibilité animations transparence barres de défilement"},
    {L"Pointeur de souris et toucher", L"ms-settings:easeofaccess-mousepointer", L"accessibilité curseur taille du pointeur"},
    {L"Loupe", L"ms-settings:easeofaccess-magnifier", L"accessibilité zoom agrandir"},
    {L"Filtres de couleur", L"ms-settings:easeofaccess-colorfilter", L"accessibilité daltonisme"},
    {L"Thèmes de contraste", L"ms-settings:easeofaccess-highcontrast", L"accessibilité contraste élevé"},
    {L"Narrateur", L"ms-settings:easeofaccess-narrator", L"accessibilité lecteur d'écran"},
    {L"Sous-titres", L"ms-settings:easeofaccess-closedcaptioning", L"accessibilité"},
    {L"Souris", L"ms-settings:mousetouchpad", L"pointeur vitesse défilement molette boutons"},
    {L"Pavé tactile", L"ms-settings:devices-touchpad", L"touchpad gestes"},
    {L"Imprimantes et scanners", L"ms-settings:printers", L"imprimante scanner impression numériser"},
    {L"Exécution automatique", L"ms-settings:autoplay", L"autoplay clé usb"},
    {L"Confidentialité et sécurité", L"ms-settings:privacy", L"autorisations"},
    {L"Sécurité Windows", L"ms-settings:windowsdefender", L"antivirus defender pare-feu"},
    {L"Localisation", L"ms-settings:privacy-location", L"emplacement position gps"},
    {L"Caméra", L"ms-settings:privacy-webcam", L"webcam autorisations"},
    {L"Microphone", L"ms-settings:privacy-microphone", L"micro autorisations"},
    {L"Localiser mon appareil", L"ms-settings:findmydevice", L"perdu volé"},
    {L"Windows Update", L"ms-settings:windowsupdate", L"mises à jour mise à jour"},
};

// Forme comparée : minuscules, sans accents, apostrophe droite, « œ » en « oe », espaces simples sans bords.
std::wstring fold(std::wstring_view text) {
    std::wstring out;
    bool space = false;
    for (wchar_t c : foldForSearch(text)) {
        if (c == L' ' || c == L'\t' || c == L' ' || c == L' ') {
            space = !out.empty();
            continue;
        }
        if (space) out += L' ';
        space = false;
        if (c == L'œ') out += L"oe";
        else if (c == L'’') out += L'\'';
        else out += c;
    }
    return out;
}

bool wordStart(const std::wstring& s, std::size_t pos) {
    return pos == 0 || std::wstring_view(L" -(._'/").find(s[pos - 1]) != std::wstring_view::npos;
}

std::wstring withoutHyphens(std::wstring s) {
    s.erase(std::remove(s.begin(), s.end(), L'-'), s.end());
    return s;
}

bool atWordStart(const std::wstring& text, const std::wstring& q) {
    for (std::size_t pos = text.find(q); pos != std::wstring::npos; pos = text.find(q, pos + 1))
        if (wordStart(text, pos)) return true;
    return false;
}

// Espaces et traits d'union interchangeables : « wi fi », « haut parleurs ».
std::wstring hyphensAsSpaces(std::wstring s) {
    std::replace(s.begin(), s.end(), L'-', L' ');
    return s;
}

// Sans espaces ni traits d'union : « wifi », « hautparleurs ».
std::wstring compact(std::wstring s) {
    s.erase(std::remove_if(s.begin(), s.end(), [](wchar_t c) { return c == L' ' || c == L'-'; }), s.end());
    return s;
}

// Pluriels tolérés : « capture d'écran » trouve « Captures d'écran ». Seulement les mots de quatre lettres ou plus
// (« pas » n'est pas le pluriel de « pa ») ; sans lastToo, pas le dernier mot, en cours de frappe ou pas un pluriel
// (« tous », « docs » trouveraient « toucher », « Dock »).
std::wstring singular(const std::wstring& s, bool lastToo = true) {
    std::wstring out;
    std::size_t i = 0;
    while (i <= s.size()) {
        std::size_t j = i;
        while (j < s.size() && s[j] != L' ' && s[j] != L'-' && s[j] != L'\'') ++j;
        std::wstring w = s.substr(i, j - i);
        if ((lastToo || j < s.size()) && w.size() >= 4 && (w.back() == L's' || w.back() == L'x')) w.pop_back();
        out += w;
        if (j < s.size()) out += s[j];
        i = j + 1;
    }
    return out;
}

// Début d'un mot, aux espaces et traits d'union près ; aux pluriels près aussi pour les noms (plural). Une variante
// vide (« -- » sans ses traits d'union) n'est pas essayée : elle trouverait tout.
bool atWordStartLoose(const std::wstring& text, const std::wstring& q, bool plural) {
    if (atWordStart(text, q)) return true;
    const std::wstring spaced = hyphensAsSpaces(q), cq = compact(q);
    if (spaced.find_first_not_of(L' ') != std::wstring::npos && atWordStart(hyphensAsSpaces(text), spaced)) return true;
    if (!cq.empty() && atWordStart(withoutHyphens(text), cq)) return true;
    if (plural) {
        const std::wstring sq = singular(q, false);
        if (!compact(sq).empty() && atWordStart(singular(text), sq)) return true;
    }
    return false;
}

// 0 : début du nom ; 1 : début d'un mot du nom ; 2 : début d'un mot-clé ; -1 : rien. q plié, non vide. Pluriels
// tolérés dans les noms seulement (dans les mots-clés, « mises à jour » trouverait aussi « mise à jour de logiciels »).
int matchGroup(const SettingsPage& page, const std::wstring& q) {
    const std::wstring title = fold(page.title), cq = compact(q), sq = singular(q, false);
    if (title.starts_with(q) || (!cq.empty() && compact(title).starts_with(cq)) ||
        (!compact(sq).empty() && singular(title).starts_with(sq)))
        return 0;
    if (atWordStartLoose(title, q, true)) return 1;
    if (atWordStartLoose(fold(page.keywords), q, false)) return 2;
    return -1;
}

} // namespace

const std::vector<SettingsPage>& settingsPages() {
    static const std::vector<SettingsPage> pages = [] {
        std::vector<SettingsPage> out;
        for (const PaneInfo& p : paneList()) {
            const auto words = paneKeywords().find(p.key);
            out.push_back({true, p.title, fromUtf8(p.key), words == paneKeywords().end() ? L"macdock" : words->second});
        }
        for (const WindowsPage& w : kWindowsPages) out.push_back({false, w.title, w.uri, w.keywords});
        return out;
    }();
    return pages;
}

std::vector<std::size_t> searchSettings(const std::vector<SettingsPage>& pages, const std::wstring& query) {
    const std::wstring q = fold(query);
    std::vector<std::size_t> groups[3];
    if (q.empty()) return {};
    for (std::size_t i = 0; i < pages.size(); ++i)
        if (const int g = matchGroup(pages[i], q); g >= 0) groups[g].push_back(i);
    std::vector<std::size_t> out = std::move(groups[0]);
    out.insert(out.end(), groups[1].begin(), groups[1].end());
    out.insert(out.end(), groups[2].begin(), groups[2].end());
    return out;
}

bool settingTitleMatch(const SettingsPage& page, const std::wstring& query) {
    const std::wstring q = fold(query);
    const int g = q.empty() ? -1 : matchGroup(page, q);
    return g == 0 || g == 1;
}

bool exactSettingTitle(const SettingsPage& page, const std::wstring& query, bool plural) {
    // Aux espaces et traits d'union près : « wifi », « wi fi » : « Wi-Fi » ; plural : aux pluriels près aussi
    // (« capture d'écran » : « Captures d'écran »).
    const std::wstring q = fold(query), t = fold(page.title);
    const std::wstring bare = compact(plural ? singular(q) : q);
    return !bare.empty() && compact(plural ? singular(t) : t) == bare;
}

} // namespace md
