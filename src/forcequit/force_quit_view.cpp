#include "force_quit_view.h"

#include <algorithm>
#include <cmath>

namespace md {

const wchar_t* const kFqTitle = L"Forcer des applications à quitter";
const wchar_t* const kFqIntro =
    L"Si une app ne répond pas pendant un moment, sélectionnez son nom et cliquez sur Forcer à quitter.";
const wchar_t* const kFqNotResponding = L"(ne répond pas)";

namespace {
float listHeight(const FqLayout& l) { return l.list.bottom - l.list.top; }
float contentHeight(std::size_t rows) { return float(rows) * kFqRow + 2 * kFqListInset; }
}  // namespace

FqLayout layoutForceQuit(ui::Painter& p, const FqView& v) {
    FqLayout l;
    l.lights = D2D1::Point2F(kFqMargin, kFqTitleBar / 2);
    // Titre centré dans la fenêtre ; trop large pour tenir entre les pastilles et le bord opposé, centré dans la place
    // qui reste à droite des pastilles.
    const float lightsEnd = l.lights.x + 2 * ui::kLightSpacing + ui::kLightRadius + 10;
    const float titleW = p.textWidth(kFqTitle, kFqFontBody, DWRITE_FONT_WEIGHT_SEMI_BOLD) + 4;
    l.title = titleW <= kFqWidth - 2 * lightsEnd ? D2D1::RectF(lightsEnd, 0, kFqWidth - lightsEnd, kFqTitleBar)
                                                 : D2D1::RectF(lightsEnd, 0, kFqWidth - 12, kFqTitleBar);
    const float textWidth = kFqWidth - 2 * kFqMargin;
    l.intro = D2D1::RectF(kFqMargin, kFqTitleBar + 2, kFqWidth - kFqMargin,
                          kFqTitleBar + 2 + p.paragraphHeight(kFqIntro, textWidth, kFqFontBody));
    const float buttonW = ui::buttonWidth(p, v.buttonLabel);
    l.button = D2D1::RectF(kFqWidth - kFqMargin - buttonW, kFqHeight - 16 - ui::metrics::buttonHeight, kFqWidth - kFqMargin,
                           kFqHeight - 16);
    l.list = D2D1::RectF(kFqMargin, l.intro.bottom + 10, kFqWidth - kFqMargin, l.button.top - 14);
    const float hintRight = l.button.left - 12;
    const float hintH = v.hint.empty() ? 0 : p.paragraphHeight(v.hint, hintRight - kFqMargin, kFqFontHint);
    const float cy = (l.button.top + l.button.bottom) / 2;
    l.hint = D2D1::RectF(kFqMargin, cy - hintH / 2, hintRight, cy + hintH / 2);
    return l;
}

D2D1_RECT_F rowRect(const FqLayout& l, int i, float scroll) {
    const float top = l.list.top + kFqListInset + float(i) * kFqRow - scroll;
    return D2D1::RectF(l.list.left + kFqListInset, top, l.list.right - kFqListInset, top + kFqRow);
}

int rowAt(const FqLayout& l, std::size_t rows, float scroll, float x, float y) {
    if (x < l.list.left + kFqListInset || x >= l.list.right - kFqListInset || y < l.list.top || y >= l.list.bottom) return -1;
    const float offset = y - (l.list.top + kFqListInset) + scroll;
    if (offset < 0) return -1;
    const int i = int(std::floor(offset / kFqRow));
    return i < int(rows) ? i : -1;
}

float maxScroll(const FqLayout& l, std::size_t rows) { return std::max(0.f, contentHeight(rows) - listHeight(l)); }

float scrollToShow(const FqLayout& l, std::size_t rows, float scroll, int i) {
    if (i < 0) return std::clamp(scroll, 0.f, maxScroll(l, rows));
    const float top = float(i) * kFqRow, bottom = float(i + 1) * kFqRow + 2 * kFqListInset;
    if (top < scroll) scroll = top;
    if (bottom - scroll > listHeight(l)) scroll = bottom - listHeight(l);
    return std::clamp(scroll, 0.f, maxScroll(l, rows));
}

int lightAt(const FqLayout& l, float x, float y) {
    for (int i = 0; i < 2; ++i) {
        const float dx = x - (l.lights.x + float(i) * ui::kLightSpacing), dy = y - l.lights.y;
        if (dx * dx + dy * dy <= (ui::kLightRadius + 2) * (ui::kLightRadius + 2)) return i;
    }
    return -1;
}

void drawForceQuit(ui::Painter& p, const FqLayout& l, const FqView& v) {
    const ui::Palette& pal = p.pal();
    p.rt()->FillRectangle(D2D1::RectF(0, 0, kFqWidth, kFqHeight), p.brush(pal.window));
    ui::drawWindowLights(p, l.lights, v.active, v.lightsHover, v.lightPressed, 2);
    p.text(kFqTitle, l.title, kFqFontBody, v.active ? pal.text : pal.secondaryText, DWRITE_FONT_WEIGHT_SEMI_BOLD,
           DWRITE_TEXT_ALIGNMENT_CENTER);
    p.paragraph(kFqIntro, l.intro, kFqFontBody, pal.text);

    p.fillRound(l.list, kFqListRadius, pal.group);
    p.strokeRound(l.list, kFqListRadius, pal.separator, 1);
    p.rt()->PushAxisAlignedClip(D2D1::RectF(l.list.left + 1, l.list.top + 1, l.list.right - 1, l.list.bottom - 1),
                                D2D1_ANTIALIAS_MODE_ALIASED);
    const std::wstring suffix = std::wstring(L" ") + kFqNotResponding;
    const float suffixW = p.textWidth(suffix, kFqFontBody);
    for (int i = 0; i < int(v.rows.size()); ++i) {
        const D2D1_RECT_F r = rowRect(l, i, v.scroll);
        if (r.bottom < l.list.top || r.top > l.list.bottom) continue;
        const FqRow& row = v.rows[std::size_t(i)];
        const bool selected = i == v.selected;
        if (selected) p.fillRound(r, kFqRowRadius, v.active ? pal.accent : pal.sidebarSelection);
        const bool onAccent = selected && v.active;
        const float iy = (r.top + r.bottom - kFqIcon) / 2;
        const D2D1_RECT_F icon = D2D1::RectF(r.left + 6, iy, r.left + 6 + kFqIcon, iy + kFqIcon);
        if (row.icon) p.rt()->DrawBitmap(row.icon, icon, 1.f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        else p.fillRound(icon, 5, pal.controlFill);
        const float x = icon.right + 8, right = r.right - 8;
        const float room = right - x - (row.hung ? suffixW : 0);
        const float nameW = std::min(p.textWidth(row.name, kFqFontBody), std::max(room, 0.f));
        p.text(row.name, D2D1::RectF(x, r.top, x + std::max(room, 0.f), r.bottom), kFqFontBody, onAccent ? pal.onAccent : pal.text);
        if (row.hung)   // rouge, comme macOS ; en blanc sur l'accent pour rester lisible
            p.text(suffix, D2D1::RectF(x + nameW, r.top, right, r.bottom), kFqFontBody, onAccent ? pal.onAccent : pal.danger);
    }
    p.rt()->PopAxisAlignedClip();

    if (!v.hint.empty()) p.paragraph(v.hint, l.hint, kFqFontHint, pal.secondaryText);
    if (v.buttonEnabled) {
        ui::drawButton(p, l.button, v.buttonLabel, true, v.buttonPressed);
    } else {   // sans sélection : bouton ordinaire grisé
        p.opacity = 0.5f;
        ui::drawButton(p, l.button, v.buttonLabel, false, false);
        p.opacity = 1;
    }
}

ui::SheetSpec forceQuitConfirm(const FqApp& app, const std::vector<std::wstring>& alsoQuitting) {
    if (app.explorer)
        return {L"Voulez-vous relancer l'Explorateur de fichiers ?", L"Ses fenêtres ouvertes seront fermées, puis le bureau reviendra.",
                {L"Annuler", L"Relancer"}, 1};
    std::wstring message = L"Les modifications non enregistrées seront perdues.";
    if (!alsoQuitting.empty()) {   // « A », « A et B », « A, B et C »
        std::wstring names;
        for (std::size_t i = 0; i < alsoQuitting.size(); ++i)
            names += (i == 0 ? L"" : i + 1 == alsoQuitting.size() ? L" et " : L", ") + alsoQuitting[i];
        message += L" " + names + (alsoQuitting.size() == 1 ? L" quittera" : L" quitteront") +
                   L" aussi : elles partagent le même processus.";
    }
    return {L"Voulez-vous forcer « " + app.name + L" » à quitter ?", message, {L"Annuler", L"Forcer à quitter"}, 1};
}

} // namespace md
