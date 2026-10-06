// Règles de la capture de l'arrière-plan (pur) : régions, filtrage des zones modifiées, reprise, HDR.
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace md {

struct IRect {
    long left = 0, top = 0, right = 0, bottom = 0;
};

bool intersects(const IRect& a, const IRect& b);   // rectangles vides : false
// Région de l'écran virtuel → coordonnées de la sortie (outputDesktop = DesktopCoordinates), bornée ; vide si hors sortie.
IRect toOutputRect(const IRect& screen, const IRect& outputDesktop);
bool anyIntersects(const IRect& region, const IRect* rects, std::size_t n);
// Rotation DXGI (1 = IDENTITY, 0 = UNSPECIFIED) : seules ces deux valeurs sont prises en charge.
bool rotationSupported(int dxgiRotation);
// Échec de mise en place de la capture qui ne se résoudra pas seul (DXGI_ERROR_UNSUPPORTED, écran tourné) :
// la capture s'arrête jusqu'au prochain changement d'affichage au lieu de réessayer toutes les 2 s.
bool permanentCaptureFailure(long hr, bool rotationUnsupported);
// SDRWhiteLevel de DISPLAYCONFIG (1000 = 80 nits) → facteur scRGB du blanc SDR ; 1 si invalide.
float sdrWhiteScale(unsigned sdrWhiteLevel);

class CaptureBackoff {   // délais de reprise : 250, 500, 1000, 2000, 2000… ms
public:
    unsigned nextDelayMs();
    void reset() { next_ = 250; }

private:
    unsigned next_ = 250;
};

// Compare une image réduite (pixels BGRA8, rowPitch octets par ligne) à la précédente. Sert à ignorer les
// compositions qui ne changent rien sous le Dock (en HDR, Windows signale l'écran entier comme modifié).
class ChangeGate {
public:
    bool changed(const std::uint8_t* pixels, unsigned w, unsigned h, unsigned rowPitch);
    void reset() { last_.clear(); w_ = h_ = 0; }

private:
    std::vector<std::uint8_t> last_;
    unsigned w_ = 0, h_ = 0;
};

} // namespace md
