// Curseurs façon macOS dessinés par le code (aucune ressource Apple) : flèche noire bordée de blanc, flèches de
// redimensionnement, croix, interdit, attente (anneau gris qui tourne).
#pragma once
#include <windows.h>

#include <vector>

#include "../core/bgra_image.h"

namespace md {

enum class CursorKind { Arrow, AppStarting, Wait, SizeNS, SizeWE, SizeNWSE, SizeNESW, SizeAll, Crosshair, No };
constexpr CursorKind kThemeCursors[] = {CursorKind::Arrow,    CursorKind::AppStarting, CursorKind::Wait,    CursorKind::SizeNS,
                                        CursorKind::SizeWE,   CursorKind::SizeNWSE,    CursorKind::SizeNESW, CursorKind::SizeAll,
                                        CursorKind::Crosshair, CursorKind::No};

const wchar_t* cursorRegistryName(CursorKind k);   // valeur de HKCU\Control Panel\Cursors
const wchar_t* cursorFileName(CursorKind k);       // arrow.cur, wait.ani…
bool cursorAnimated(CursorKind k);                 // Wait, AppStarting

struct CursorFrame {
    BgraImage image;
    POINT hotspot{};
};
// Une image (fixe) ou 12 (animé), de size × size pixels.
std::vector<CursorFrame> cursorFrames(CursorKind k, int size);

} // namespace md
