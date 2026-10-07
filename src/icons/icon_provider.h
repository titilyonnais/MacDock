// Extraction et mise en forme des icônes du Dock (Shell + WIC).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace md {

// Fichier et date de modification : un contenu nouveau sous le même chemin donne une autre image.
struct FileRef {
    std::wstring path;
    std::uint64_t modified = 0;   // FILETIME ; 0 = inconnue
    FileRef() = default;
    FileRef(std::wstring p, std::uint64_t m = 0) : path(std::move(p)), modified(m) {}
    bool operator==(const FileRef&) const = default;
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
    void clear() { cache_.clear(); }

private:
    ImagePtr build(const std::wstring& key, const std::wstring& parsingName, int px);
    // Forme visible (shape x shape) posée au centre d'une case px x px, avec l'ombre portée.
    ImagePtr finish(std::vector<std::uint8_t> shaped, int shape, int px) const;

    bool strict_ = true;
    bool dark_ = false;
    double shapeRatio_ = 824.0 / 1024.0;
    double cornerRatio_ = 185.4 / 824.0;
    double jailInset_ = 0.16;
    double shadowOpacity_ = 0.5;
    std::wstring customDir_;
    std::map<std::wstring, ImagePtr> cache_;
};

} // namespace md
