#include "ui_theme.h"

namespace md::ui {

Palette palette(bool dark) {
    Palette p;
    if (!dark) {
        p.window = rgb(0xFFFFFF);
        p.sidebarTint = rgb(0xF2F2F4, 0.55f);
        p.sidebarSelection = rgb(0x000000, 0.10f);
        p.group = rgb(0x000000, 0.035f);
        p.separator = rgb(0x000000, 0.10f);
        p.text = rgb(0x000000, 0.85f);
        p.secondaryText = rgb(0x000000, 0.50f);
        p.tertiaryText = rgb(0x000000, 0.26f);
        p.accent = rgb(0x0088FF);
        p.switchOff = rgb(0x000000, 0.10f);
        p.knobEdge = rgb(0x000000, 0.10f);
        p.controlFill = rgb(0x000000, 0.06f);
        p.segmentSelected = rgb(0xFFFFFF);
        p.focusRing = {0.f, 103 / 255.f, 244 / 255.f, 0.5f};
        p.menuBackground = rgb(0xF6F6F7, 0.98f);
        p.menuEdge = rgb(0x000000, 0.12f);
        p.shadow = rgb(0x000000, 0.18f);
        p.buttonFill = rgb(0x000000, 0.08f);
        p.buttonEdge = rgb(0x000000, 0.06f);
        p.danger = rgb(0xFF383C);
        p.dim = rgb(0x000000, 0.22f);
        p.sheetBackground = rgb(0xF6F6F8, 0.98f);
        p.sheetEdge = rgb(0x000000, 0.10f);
    } else {
        p.window = rgb(0x1E1E1E);
        p.sidebarTint = rgb(0x262628, 0.55f);
        p.sidebarSelection = rgb(0xFFFFFF, 0.12f);
        p.group = rgb(0xFFFFFF, 0.05f);
        p.separator = rgb(0xFFFFFF, 0.10f);
        p.text = rgb(0xFFFFFF, 0.85f);
        p.secondaryText = rgb(0xFFFFFF, 0.55f);
        p.tertiaryText = rgb(0xFFFFFF, 0.25f);
        p.accent = rgb(0x0091FF);
        p.switchOff = rgb(0xFFFFFF, 0.16f);
        p.knobEdge = rgb(0x000000, 0.25f);
        p.controlFill = rgb(0xFFFFFF, 0.10f);
        p.segmentSelected = rgb(0xFFFFFF, 0.26f);
        p.focusRing = {26 / 255.f, 169 / 255.f, 1.f, 0.5f};
        p.menuBackground = rgb(0x2A2A2C, 0.98f);
        p.menuEdge = rgb(0xFFFFFF, 0.14f);
        p.shadow = rgb(0x000000, 0.45f);
        p.buttonFill = rgb(0xFFFFFF, 0.14f);
        p.buttonEdge = rgb(0xFFFFFF, 0.06f);
        p.danger = rgb(0xFF4245);
        p.dim = rgb(0x000000, 0.40f);
        p.sheetBackground = rgb(0x2C2C2E, 0.98f);
        p.sheetEdge = rgb(0xFFFFFF, 0.12f);
    }
    p.onAccent = rgb(0xFFFFFF);
    p.knob = rgb(0xFFFFFF);
    return p;
}

}  // namespace md::ui
