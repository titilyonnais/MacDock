// Règles de la capture de l'arrière-plan (pur) : régions, filtrage des zones modifiées, reprise, HDR.
#pragma once
#include <cstddef>

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
// SDRWhiteLevel de DISPLAYCONFIG (1000 = 80 nits) → facteur scRGB du blanc SDR ; 1 si invalide.
float sdrWhiteScale(unsigned sdrWhiteLevel);

class CaptureBackoff {   // délais de reprise : 250, 500, 1000, 2000, 2000… ms
public:
    unsigned nextDelayMs();
    void reset() { next_ = 250; }

private:
    unsigned next_ = 250;
};

} // namespace md
