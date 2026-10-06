// Extraction et mise en forme des icônes du Dock (Shell + WIC).
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace md {

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
    void setJailInset(double inset);         // marge relative de l'icône dans sa plaque (« icon jail »)

    // key : identifiant stable (appId, chemin…) utilisé pour l'icône personnalisée et le cache.
    // parsingName : nom Shell (chemin, .lnk, shell:AppsFolder\AUMID, ::{CLSID}).
    ImagePtr get(const std::wstring& key, const std::wstring& parsingName, int px);
    ImagePtr appsButton(int px);            // icône « Apps » dessinée par le code
    void clear() { cache_.clear(); }

private:
    ImagePtr build(const std::wstring& key, const std::wstring& parsingName, int px);

    bool strict_ = true;
    bool dark_ = false;
    double jailInset_ = 0.16;
    std::wstring customDir_;
    std::map<std::wstring, ImagePtr> cache_;
};

} // namespace md
