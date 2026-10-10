// Extraction et mise en forme des icônes du Dock (Shell + WIC).
#pragma once
#include <chrono>
#include <functional>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "squircle.h"

namespace md {

// Fichier et date de modification : un contenu nouveau sous le même chemin donne une autre image.
struct FileRef {
    std::wstring path;
    std::uint64_t modified = 0;   // FILETIME ; 0 = inconnue
    FileRef() = default;
    FileRef(std::wstring p, std::uint64_t m = 0) : path(std::move(p)), modified(m) {}
    bool operator==(const FileRef&) const = default;
};

// Réglages des icônes du Dock, copiés dans chaque travail du fil des icônes (plan 67).
struct IconStyle {
    bool strict = true;
    bool dark = false;
    double shapeRatio = kIconShapeRatio;
    double cornerRatio = kIconCornerRatio;
    double jailInset = 0.16;
    double shadowOpacity = 0.5;
    std::wstring customDir;
    bool operator==(const IconStyle&) const = default;
};

// Ce qu'il faut préparer, avec la copie des réglages des icônes au moment de la demande.
enum class IconJobKind { App, Stack, AppsButton };
struct IconSpec {
    IconJobKind kind = IconJobKind::App;
    std::wstring key;             // App : clé (appId, chemin…) ; Stack : clé de la pile
    std::wstring parsingName;     // App : nom Shell
    std::vector<FileRef> files;   // Stack : derniers éléments (le premier au-dessus)
    int px = 0;
    IconStyle style;
};

// Travail du fil des icônes (plan 67), demandé par le fournisseur du Dock pendant son rendu courant.
struct IconJob {
    std::wstring cacheKey;          // identité de l'image dans le cache du Dock
    std::uint64_t generation = 0;   // génération du cache du Dock au moment de la demande
    int priority = 0;               // 0 : case du Dock ; 1 : aperçu de pile, préparé après
    IconSpec spec;
};

class IconProvider {
public:
    struct Image {
        int size = 0;                       // carré size x size
        std::vector<std::uint8_t> bgra;     // BGRA prémultiplié, stride = size*4
    };
    using ImagePtr = std::shared_ptr<const Image>;

    void setStrictTahoe(bool strict);
    void setDark(bool dark);
    void setCustomDir(std::wstring dir) { customDir_ = std::move(dir); clear(); }
    // Grille Apple : forme visible / case, rayon / forme, marge de l'icône dans sa plaque (« icon jail »),
    // opacité de l'ombre portée. Vide le cache si une valeur change.
    void setGrid(double shapeRatio, double cornerRatio, double jailInset, double shadowOpacity);

    // key : identifiant stable (appId, chemin…) utilisé pour l'icône personnalisée et le cache.
    // parsingName : nom Shell (chemin, .lnk, shell:AppsFolder\AUMID, ::{CLSID}).
    ImagePtr get(const std::wstring& key, const std::wstring& parsingName, int px);
    // Budget d'une image du Dock (plan 62) : une fois dépassé, get() rend nullptr pour une icône pas encore prête (sa
    // case reste vide) et deferred() le signale ; elle sera préparée à l'image suivante. Hors budget : tout de suite.
    void beginBudget(double ms) {
        budgetEnd_ = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                             std::chrono::duration<double, std::milli>(ms));
        budget_ = true;
        deferred_ = false;
    }
    void endBudget() {
        budget_ = false;
        if (!deferred_) stale_.clear();   // tout est refait : la réserve ne sert plus
    }
    bool deferred() const { return deferred_; }
    // Plan 67 : entre beginAsync et endAsync (rendu courant du Dock), une image absente du cache est demandée à `sink`
    // (copie des réglages et génération comprises) au lieu d'être préparée ici : get(), composeStack() et appsButton()
    // rendent l'ancienne image ou nullptr, et deferred() le signale. adopt() range ensuite le résultat.
    using AsyncSink = std::function<void(IconJob)>;
    void beginAsync(AsyncSink sink);
    // Dans la dernière image en mode « hors du fil » : une case (app, Corbeille, Apps, dossier) ou un aperçu de pile
    // attendait (mesure du mode diagnostic).
    bool deferredTiles() const { return deferredTiles_; }
    bool deferredStacks() const { return deferredStacks_; }
    void endAsync();
    // Résultat du fil des icônes : rangé s'il est de la génération courante et pas déjà préparé ici entre-temps.
    bool adopt(const std::wstring& cacheKey, std::uint64_t generation, ImagePtr image);
    // Monte à chaque vidage du cache (thème, réglages des icônes, écran).
    std::uint64_t generation() const { return generation_; }
    // Images demandées sans être prêtes (mode « hors du fil » ou budget dépassé), depuis la création : un appel qui fait
    // monter ce compte a laissé sa case en attente.
    std::uint64_t deferrals() const { return deferrals_; }
    IconStyle style() const;   // réglages actuels, copiés dans chaque travail
    // Fil des icônes : cache et réserve oubliés (le Dock a vidé le sien).
    void forget() {
        cache_.clear();
        stale_.clear();
    }
    ImagePtr appsButton(int px);            // icône « Apps » dessinée par le code
    ImagePtr trash(bool full, int px);      // icône système de la Corbeille, vide ou pleine
    // Élément de pile : vignette Shell (images…) ou icône du fichier, telle quelle (ni plaque ni forme),
    // px x px ; icône générique du type si le fichier n'existe plus.
    ImagePtr file(const std::wstring& path, int px, std::uint64_t modified = 0);
    // Icône du fichier telle que l'Explorateur la montre (liste système, rapide : jamais de vignette), px x px.
    ImagePtr fileIcon(const std::wstring& path, int px);
    // Icône du type d'après le seul nom (« rapport.docx ») : jamais d'accès au fichier, au disque ni au réseau.
    ImagePtr extensionIcon(const std::wstring& name, int px);
    // Pile « comme Pile » : images des éléments (le premier au-dessus) empilées dans la forme d'icône, avec
    // l'ombre des icônes du Dock ; nullptr si aucune image.
    ImagePtr composeStack(const std::wstring& key, const std::vector<FileRef>& files, int px);
    // Images refaites (thème, réglages des icônes) : les anciennes restent en réserve jusqu'à ce que les nouvelles
    // soient prêtes, pour que les cases ne se vident pas pendant le rechargement progressif.
    void clear() {
        for (auto& [k, v] : cache_) stale_[k] = std::move(v);
        cache_.clear();
        ++generation_;   // plan 67 : les résultats du fil des icônes demandés avant sont dépassés
    }

private:
    bool budget_ = false, deferred_ = false;
    AsyncSink async_;
    std::uint64_t generation_ = 0, deferrals_ = 0;
    bool deferredTiles_ = false, deferredStacks_ = false;
    ImagePtr requestAsync(IconSpec spec, const std::wstring& cacheKey, int priority);
    std::map<std::wstring, ImagePtr> stale_;   // anciennes images, affichées en attendant les nouvelles
    ImagePtr deferStale(const std::wstring& cacheKey);   // budget dépassé : l'ancienne image s'il y en a une
    std::chrono::steady_clock::time_point budgetEnd_{};
    ImagePtr build(const std::wstring& key, const std::wstring& parsingName, int px);
    // Forme visible (shape x shape) posée au centre d'une case px x px, avec l'ombre portée.
    ImagePtr finish(std::vector<std::uint8_t> shaped, int shape, int px) const;

    bool strict_ = true;
    bool dark_ = false;
    double shapeRatio_ = kIconShapeRatio;
    double cornerRatio_ = kIconCornerRatio;   // forme de macOS 26 (plan 60), remplacée par setGrid
    double jailInset_ = 0.16;
    double shadowOpacity_ = 0.5;
    std::wstring customDir_;
    std::map<std::wstring, ImagePtr> cache_;
};

} // namespace md
