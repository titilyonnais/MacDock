#include "ui_draw.h"

#include <algorithm>
#include <cmath>

namespace md::ui {

using Microsoft::WRL::ComPtr;

namespace {
Rgba mix(Rgba a, Rgba b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Rgba withAlpha(Rgba c, float a) { return {c.r, c.g, c.b, c.a * a}; }
D2D1_RECT_F inflate(D2D1_RECT_F r, float d) { return {r.left - d, r.top - d, r.right + d, r.bottom + d}; }
bool darkPalette(const Palette& p) { return p.window.r < 0.5f; }

// Bouton blanc d'un interrupteur ou d'un curseur : ombre douce, liseré, blanc.
void knob(Painter& p, D2D1_POINT_2F c, float rx, float ry) {
    ID2D1RenderTarget* rt = p.rt();
    rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y + 0.75f), rx + 0.75f, ry + 0.75f), p.brush(withAlpha(p.pal().shadow, 0.6f)));
    rt->FillEllipse(D2D1::Ellipse(c, rx + 0.5f, ry + 0.5f), p.brush(p.pal().knobEdge));
    rt->FillEllipse(D2D1::Ellipse(c, rx, ry), p.brush(p.pal().knob));
}
} // namespace

Painter::Painter(ID2D1RenderTarget* rt, IDWriteFactory* dwrite, const Palette& palette, std::wstring font)
    : rt_(rt), dwrite_(dwrite), pal_(palette), font_(std::move(font)) {
    if (font_.empty()) font_ = interfaceFont(dwrite_);
    rt_->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brush_);
}

ID2D1Factory* Painter::factory() const {
    ComPtr<ID2D1Factory> f;
    rt_->GetFactory(&f);
    return f.Get();   // la cible garde sa fabrique vivante
}

ID2D1SolidColorBrush* Painter::brush(Rgba c) {
    brush_->SetColor(D2D1::ColorF(c.r, c.g, c.b, c.a * opacity));
    return brush_.Get();
}

IDWriteTextFormat* Painter::format(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
    for (auto& f : formats_)
        if (f.size == size && f.weight == weight && f.align == align) return f.format.Get();
    Format f{size, weight, align, nullptr};
    if (FAILED(dwrite_->CreateTextFormat(font_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, L"fr-FR", &f.format)))
        return nullptr;
    f.format->SetTextAlignment(align);
    f.format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    f.format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    DWRITE_TRIMMING trim{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    ComPtr<IDWriteInlineObject> ellipsis;
    dwrite_->CreateEllipsisTrimmingSign(f.format.Get(), &ellipsis);
    f.format->SetTrimming(&trim, ellipsis.Get());
    formats_.push_back(f);
    return formats_.back().format.Get();
}

void Painter::fillRound(D2D1_RECT_F r, float radius, Rgba c) {
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(c));
}

void Painter::strokeRound(D2D1_RECT_F r, float radius, Rgba c, float width) {
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush(c), width);
}

void Painter::fillCircle(D2D1_POINT_2F c, float radius, Rgba color) {
    rt_->FillEllipse(D2D1::Ellipse(c, radius, radius), brush(color));
}

void Painter::text(const std::wstring& s, D2D1_RECT_F r, float size, Rgba c, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align) {
    if (s.empty()) return;
    if (IDWriteTextFormat* f = format(size, weight, align))
        rt_->DrawText(s.c_str(), UINT32(s.size()), f, r, brush(c), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void Painter::paragraph(const std::wstring& s, D2D1_RECT_F r, float size, Rgba c, DWRITE_FONT_WEIGHT weight) {
    if (s.empty()) return;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dwrite_->CreateTextFormat(font_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, L"fr-FR", &f)))
        return;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    rt_->DrawText(s.c_str(), UINT32(s.size()), f.Get(), r, brush(c));
}

float Painter::paragraphHeight(const std::wstring& s, float width, float size, DWRITE_FONT_WEIGHT weight) {
    if (s.empty()) return 0;
    ComPtr<IDWriteTextFormat> f;
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwrite_->CreateTextFormat(font_.c_str(), nullptr, weight, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                         size, L"fr-FR", &f)))
        return 0;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    if (FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), f.Get(), width, 10000, &layout))) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return std::ceil(m.height);
}

float Painter::textWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight) {
    IDWriteTextFormat* f = format(size, weight, DWRITE_TEXT_ALIGNMENT_LEADING);
    ComPtr<IDWriteTextLayout> layout;
    if (!f || FAILED(dwrite_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, 10000, 100, &layout))) return 0;
    DWRITE_TEXT_METRICS m{};
    layout->GetMetrics(&m);
    return m.widthIncludingTrailingWhitespace;
}

std::wstring interfaceFont(IDWriteFactory* dwrite) {
    ComPtr<IDWriteFontCollection> fonts;
    if (dwrite) dwrite->GetSystemFontCollection(&fonts, FALSE);
    for (const wchar_t* f : {L"SF Pro Text", L"SF Pro", L"Inter", L"Segoe UI Variable Text", L"Segoe UI"}) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (fonts && SUCCEEDED(fonts->FindFamilyName(f, &index, &exists)) && exists) return f;
    }
    return L"Segoe UI";
}

void drawWindowBackground(Painter& p, float width, float height) {
    const Panel panel = sidebarPanel(height);
    const D2D1_ROUNDED_RECT glass = D2D1::RoundedRect(D2D1::RectF(panel.left, panel.top, panel.right, panel.bottom), panel.radius,
                                                      panel.radius);
    ID2D1Factory* f = p.factory();
    ComPtr<ID2D1RoundedRectangleGeometry> hole;
    f->CreateRoundedRectangleGeometry(glass, &hole);
    // Une forme moins le panneau : le contenu autour, puis chaque anneau de l'ombre (jamais sur le verre).
    auto fillOutside = [&](ID2D1Geometry* shape, Rgba color) {
        ComPtr<ID2D1PathGeometry> path;
        ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(f->CreatePathGeometry(&path)) || FAILED(path->Open(&sink))) return;
        shape->CombineWithGeometry(hole.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
        sink->Close();
        p.rt()->FillGeometry(path.Get(), p.brush(color));
    };
    ComPtr<ID2D1RectangleGeometry> all;
    f->CreateRectangleGeometry(D2D1::RectF(0, 0, width, height), &all);
    fillOutside(all.Get(), p.pal().window);
    for (int k = 4; k >= 1; --k) {   // ombre douce, plus marquée près du panneau
        const float d = float(k) * 1.5f;
        ComPtr<ID2D1RoundedRectangleGeometry> ring;
        f->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(panel.left - d, panel.top - d * 0.6f, panel.right + d,
                                                                       panel.bottom + d * 1.4f),
                                                            panel.radius + d, panel.radius + d),
                                          &ring);
        fillOutside(ring.Get(), withAlpha(p.pal().shadow, 0.10f));
    }
    p.rt()->FillRoundedRectangle(glass, p.brush(p.pal().sidebarTint));   // verre : le fond acrylique transparaît
    p.rt()->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(panel.left + 0.5f, panel.top + 0.5f, panel.right - 0.5f, panel.bottom - 0.5f),
                                                   panel.radius - 0.5f, panel.radius - 0.5f),
                                 p.brush(p.pal().sidebarEdge), 1);
}

void drawSwitch(Painter& p, D2D1_RECT_F r, float progress, bool pressed) {
    const float h = r.bottom - r.top, w = r.right - r.left, t = std::clamp(progress, 0.f, 1.f);
    p.fillRound(r, h / 2, p.pal().switchOff);
    p.fillRound(r, h / 2, withAlpha(p.pal().accent, t));
    const float kr = h / 2 - 2, stretch = pressed ? 3.f : 0.f;   // le bouton s'étire sous le doigt, comme sur Mac
    const float cx = r.left + 2 + kr + stretch / 2 + t * (w - 4 - 2 * kr - stretch);
    knob(p, D2D1::Point2F(cx, (r.top + r.bottom) / 2), kr + stretch / 2, kr);
}

void drawSlider(Painter& p, float left, float right, float cy, float t, bool pressed) {
    const float x = left + std::clamp(t, 0.f, 1.f) * (right - left), half = metrics::sliderTrack / 2;
    p.fillRound(D2D1::RectF(left, cy - half, right, cy + half), half, p.pal().switchOff);
    if (x > left) p.fillRound(D2D1::RectF(left, cy - half, x, cy + half), half, p.pal().accent);
    const float kr = metrics::knob / 2 - 1;
    knob(p, D2D1::Point2F(x, cy), kr + (pressed ? 1.f : 0.f), kr + (pressed ? 1.f : 0.f));
}

void drawPopup(Painter& p, D2D1_RECT_F r, const std::wstring& text, bool pressed) {
    p.fillRound(r, 6, withAlpha(p.pal().controlFill, pressed ? 2.f : 1.f));
    p.text(text, D2D1::RectF(r.left + 9, r.top, r.right - 20, r.bottom), metrics::fontBody, p.pal().text);
    // Double chevron (⌃⌄) à droite.
    const float cx = r.right - 11, cy = (r.top + r.bottom) / 2;
    ID2D1SolidColorBrush* ink = p.brush(withAlpha(p.pal().text, 0.75f));
    p.rt()->DrawLine(D2D1::Point2F(cx - 3, cy - 2), D2D1::Point2F(cx, cy - 5), ink, 1.3f);
    p.rt()->DrawLine(D2D1::Point2F(cx, cy - 5), D2D1::Point2F(cx + 3, cy - 2), ink, 1.3f);
    p.rt()->DrawLine(D2D1::Point2F(cx - 3, cy + 2), D2D1::Point2F(cx, cy + 5), ink, 1.3f);
    p.rt()->DrawLine(D2D1::Point2F(cx, cy + 5), D2D1::Point2F(cx + 3, cy + 2), ink, 1.3f);
}

float popupWidth(Painter& p, const std::wstring& text) { return std::ceil(p.textWidth(text, metrics::fontBody)) + 9 + 24; }

void drawSegmented(Painter& p, D2D1_RECT_F r, const std::vector<std::wstring>& labels, int selected) {
    const int n = int(labels.size());
    if (n == 0) return;
    p.fillRound(r, 7, p.pal().controlFill);
    const float w = (r.right - r.left) / float(n);
    if (selected >= 0 && selected < n) {
        const D2D1_RECT_F s{r.left + selected * w + 2, r.top + 2, r.left + (selected + 1) * w - 2, r.bottom - 2};
        p.fillRound(D2D1::RectF(s.left, s.top + 0.75f, s.right, s.bottom + 0.75f), 5, withAlpha(p.pal().shadow, 0.5f));
        p.fillRound(s, 5, p.pal().segmentSelected);
    }
    for (int i = 1; i < n; ++i)   // séparateurs, sauf de part et d'autre du segment choisi
        if (i != selected && i != selected + 1) {
            const float x = r.left + i * w;
            p.rt()->DrawLine(D2D1::Point2F(x, r.top + 5), D2D1::Point2F(x, r.bottom - 5), p.brush(p.pal().separator), 1);
        }
    for (int i = 0; i < n; ++i)
        p.text(labels[std::size_t(i)], D2D1::RectF(r.left + i * w + 4, r.top, r.left + (i + 1) * w - 4, r.bottom), metrics::fontBody,
               p.pal().text, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
}

float segmentedWidth(Painter& p, const std::vector<std::wstring>& labels) {
    float widest = 0;
    for (const auto& l : labels) widest = std::max(widest, p.textWidth(l, metrics::fontBody));
    return float(labels.size()) * (std::ceil(widest) + 2 * metrics::segmentPadding);
}

void drawMenu(Painter& p, D2D1_RECT_F r, const std::vector<std::wstring>& items, int checked, int hover) {
    for (int k = 3; k >= 1; --k)   // ombre portée douce
        p.fillRound(D2D1::RectF(r.left - k * 2.f, r.top - k * 1.f, r.right + k * 2.f, r.bottom + k * 3.f), metrics::menuRadius + k * 2,
                    withAlpha(p.pal().shadow, 0.18f));
    p.fillRound(r, metrics::menuRadius, p.pal().menuBackground);
    p.strokeRound(r, metrics::menuRadius, p.pal().menuEdge, 0.5f);
    float y = r.top + metrics::menuPadding;
    for (int i = 0; i < int(items.size()); ++i, y += metrics::menuItem) {
        const D2D1_RECT_F row{r.left + 5, y, r.right - 5, y + metrics::menuItem};
        const bool hot = i == hover;
        if (hot) p.fillRound(row, 5, p.pal().accent);
        const Rgba ink = hot ? p.pal().onAccent : p.pal().text;
        if (i == checked) {   // ✓
            const float cx = r.left + 15, cy = y + metrics::menuItem / 2;
            ID2D1SolidColorBrush* b = p.brush(ink);
            p.rt()->DrawLine(D2D1::Point2F(cx - 4, cy), D2D1::Point2F(cx - 1.2f, cy + 3), b, 1.6f);
            p.rt()->DrawLine(D2D1::Point2F(cx - 1.2f, cy + 3), D2D1::Point2F(cx + 4, cy - 4), b, 1.6f);
        }
        p.text(items[std::size_t(i)], D2D1::RectF(r.left + 26, y, r.right - 12, y + metrics::menuItem), metrics::fontBody, ink);
    }
}

float menuWidth(Painter& p, const std::vector<std::wstring>& items) {
    float widest = 0;
    for (const auto& i : items) widest = std::max(widest, p.textWidth(i, metrics::fontBody));
    return std::ceil(widest) + 26 + 24;
}

void drawSearchField(Painter& p, D2D1_RECT_F r, const std::wstring& text, bool focused) {
    const float h = r.bottom - r.top, cy = (r.top + r.bottom) / 2;
    p.fillRound(r, h / 2, p.pal().controlFill);
    ID2D1SolidColorBrush* ink = p.brush(p.pal().secondaryText);
    p.rt()->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(r.left + 14, cy - 1), 4.5f, 4.5f), ink, 1.4f);
    p.rt()->DrawLine(D2D1::Point2F(r.left + 17.3f, cy + 2.3f), D2D1::Point2F(r.left + 20.5f, cy + 5.5f), ink, 1.6f);
    if (text.empty()) p.text(L"Rechercher", D2D1::RectF(r.left + 27, r.top, r.right - 8, r.bottom), metrics::fontBody, p.pal().tertiaryText);
    else p.text(text, D2D1::RectF(r.left + 27, r.top, r.right - 8, r.bottom), metrics::fontBody, p.pal().text);
    if (focused) drawFocusRing(p, r, h / 2);
}

void drawFocusRing(Painter& p, D2D1_RECT_F r, float radius) { p.strokeRound(inflate(r, 2), radius + 2, p.pal().focusRing, 3); }

void drawWindowLights(Painter& p, D2D1_POINT_2F first, bool active, bool hover, int pressed, int disabled) {
    // Pastilles plates de macOS 26 Tahoe (comme traffic_lights.cpp) : remplissage et liseré.
    static constexpr std::uint32_t kFill[3] = {0xFF5F57, 0xFEBC2E, 0x28C840}, kEdge[3] = {0xE0443E, 0xDEA123, 0x1AAB29};
    const bool dark = darkPalette(p.pal());
    for (int i = 0; i < 3; ++i) {
        const D2D1_POINT_2F c{first.x + i * kLightSpacing, first.y};
        const bool gray = !active || i == disabled;
        Rgba fill = gray ? rgb(dark ? 0x4E4F52 : 0xDDDDDD) : rgb(kFill[i]);
        const Rgba edge = gray ? rgb(dark ? 0x3E3F42 : 0xC4C3C6) : rgb(kEdge[i]);
        if (i == pressed && !gray) fill = mix(fill, rgb(0x000000), 0.22f);
        p.fillCircle(c, kLightRadius, edge);
        p.fillCircle(c, kLightRadius - 0.6f, fill);
        if (hover && !gray) {   // ×, −, +
            ID2D1SolidColorBrush* ink = p.brush(rgb(0x000000, 0.55f));
            const float a = 3.2f;
            if (i == 0) {
                p.rt()->DrawLine(D2D1::Point2F(c.x - a, c.y - a), D2D1::Point2F(c.x + a, c.y + a), ink, 1.2f);
                p.rt()->DrawLine(D2D1::Point2F(c.x - a, c.y + a), D2D1::Point2F(c.x + a, c.y - a), ink, 1.2f);
            } else {
                p.rt()->DrawLine(D2D1::Point2F(c.x - a - 0.5f, c.y), D2D1::Point2F(c.x + a + 0.5f, c.y), ink, 1.2f);
                if (i == 2) p.rt()->DrawLine(D2D1::Point2F(c.x, c.y - a - 0.5f), D2D1::Point2F(c.x, c.y + a + 0.5f), ink, 1.2f);
            }
        }
    }
}

void drawButton(Painter& p, D2D1_RECT_F r, const std::wstring& label, bool primary, bool pressed) {
    const float radius = std::min(metrics::buttonRadius, (r.bottom - r.top) / 2);
    if (primary) {
        Rgba fill = p.pal().accent;
        if (pressed) fill = mix(fill, rgb(0x000000), 0.18f);
        p.fillRound(D2D1::RectF(r.left, r.top + 0.75f, r.right, r.bottom + 0.75f), radius, withAlpha(p.pal().shadow, 0.6f));
        p.fillRound(r, radius, fill);
        // Reflet discret du haut (verre).
        p.fillRound(D2D1::RectF(r.left + 1, r.top + 1, r.right - 1, (r.top + r.bottom) / 2), radius - 1, rgb(0xFFFFFF, 0.10f));
        p.text(label, r, metrics::fontBody, p.pal().onAccent, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_TEXT_ALIGNMENT_CENTER);
        return;
    }
    p.fillRound(r, radius, withAlpha(p.pal().buttonFill, pressed ? 2.f : 1.f));
    p.strokeRound(D2D1::RectF(r.left + 0.25f, r.top + 0.25f, r.right - 0.25f, r.bottom - 0.25f), radius, p.pal().buttonEdge, 0.5f);
    p.text(label, r, metrics::fontBody, p.pal().text, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
}

float buttonWidth(Painter& p, const std::wstring& label) {
    return std::max(std::ceil(p.textWidth(label, metrics::fontBody, DWRITE_FONT_WEIGHT_MEDIUM)) + 2 * metrics::buttonPadding, 64.f);
}

void drawShortcutField(Painter& p, D2D1_RECT_F r, const std::wstring& label, bool listening, bool conflict) {
    p.fillRound(r, metrics::shortcutRadius, p.pal().controlFill);
    const D2D1_RECT_F inner{r.left + 8, r.top, r.right - 8, r.bottom};
    if (listening) {
        p.strokeRound(inflate(r, 1), metrics::shortcutRadius + 1, p.pal().accent, 2);
        p.text(L"Tapez le raccourci…", inner, metrics::fontBody, p.pal().tertiaryText, DWRITE_FONT_WEIGHT_REGULAR,
               DWRITE_TEXT_ALIGNMENT_CENTER);
    } else if (label.empty()) {
        p.text(L"Aucun", inner, metrics::fontBody, p.pal().tertiaryText, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_CENTER);
    } else {
        p.text(label, inner, metrics::fontBody, conflict ? p.pal().danger : p.pal().text, DWRITE_FONT_WEIGHT_REGULAR,
               DWRITE_TEXT_ALIGNMENT_CENTER);
    }
}

void drawValue(Painter& p, D2D1_RECT_F r, const std::wstring& text) {
    p.text(text, r, metrics::fontBody, p.pal().secondaryText, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_TEXT_ALIGNMENT_TRAILING);
}

SheetLayout layoutSheet(Painter& p, float windowWidth, float top, const SheetSpec& s) {
    namespace mt = metrics;
    SheetLayout l;
    const float w = std::min(mt::sheetWidth, windowWidth - 2 * mt::contentMargin), pad = mt::sheetPadding;
    const float left = (windowWidth - w) / 2, inner = w - 2 * pad;
    l.card = D2D1::RectF(left, top + mt::sheetTop, left + w, top + mt::sheetTop);
    float y = l.card.top + pad;
    const float titleH = p.paragraphHeight(s.title, inner, mt::fontBody, DWRITE_FONT_WEIGHT_BOLD);
    l.title = D2D1::RectF(left + pad, y, left + pad + inner, y + titleH);
    y += titleH + (s.message.empty() ? 0 : 6);
    const float messageH = p.paragraphHeight(s.message, inner, mt::fontDetail);
    l.message = D2D1::RectF(left + pad, y, left + pad + inner, y + messageH);
    y += messageH + 18;
    const int n = int(s.buttons.size());
    bool side = n <= 2;
    const float slot = n ? (inner - (n - 1) * mt::buttonGap) / float(n) : inner;
    for (const auto& b : s.buttons)
        if (buttonWidth(p, b) > slot) side = false;   // trop long : l'un sous l'autre
    l.buttons.resize(std::size_t(n));
    if (side) {
        for (int i = 0; i < n; ++i) {
            const int at = n == 2 ? (i == s.primary ? 1 : 0) : i;   // le bouton par défaut à droite
            const float x = left + pad + at * (slot + mt::buttonGap);
            l.buttons[std::size_t(i)] = D2D1::RectF(x, y, x + slot, y + mt::sheetButtonHeight);
        }
        if (n) y += mt::sheetButtonHeight;
    } else {
        int row = 0;
        for (int pass = 0; pass < 2; ++pass)   // le bouton par défaut en haut, puis les autres dans l'ordre
            for (int i = 0; i < n; ++i) {
                if ((i == s.primary) != (pass == 0)) continue;
                const float by = y + row++ * (mt::sheetButtonHeight + mt::buttonGap);
                l.buttons[std::size_t(i)] = D2D1::RectF(left + pad, by, left + pad + inner, by + mt::sheetButtonHeight);
            }
        y += n * mt::sheetButtonHeight + std::max(0, n - 1) * mt::buttonGap;
    }
    l.card.bottom = y + pad;
    return l;
}

void drawSheet(Painter& p, const SheetLayout& l, const SheetSpec& s, float windowWidth, float windowHeight, int hover,
               int pressed, float progress) {
    const float t = std::clamp(progress, 0.f, 1.f);
    p.rt()->FillRectangle(D2D1::RectF(0, 0, windowWidth, windowHeight), p.brush(withAlpha(p.pal().dim, t)));
    D2D1_MATRIX_3X2_F before;
    p.rt()->GetTransform(&before);
    p.rt()->SetTransform(D2D1::Matrix3x2F::Translation(0, -16 * (1 - t)) * before);   // descend en apparaissant
    const float keep = p.opacity;
    p.opacity = keep * t;
    const float r = metrics::sheetRadius;
    for (int k = 4; k >= 1; --k)   // ombre portée douce
        p.fillRound(D2D1::RectF(l.card.left - k * 2.f, l.card.top - k * 1.f, l.card.right + k * 2.f, l.card.bottom + k * 4.f),
                    r + k * 2, withAlpha(p.pal().shadow, 0.16f));
    p.fillRound(l.card, r, p.pal().sheetBackground);
    p.strokeRound(l.card, r, p.pal().sheetEdge, 0.5f);
    p.paragraph(s.title, l.title, metrics::fontBody, p.pal().text, DWRITE_FONT_WEIGHT_BOLD);
    p.paragraph(s.message, l.message, metrics::fontDetail, p.pal().text);
    for (int i = 0; i < int(l.buttons.size()); ++i) {
        (void)hover;
        drawButton(p, l.buttons[std::size_t(i)], s.buttons[std::size_t(i)], i == s.primary, i == pressed);
    }
    p.opacity = keep;
    p.rt()->SetTransform(before);
}

int sheetButtonAt(const SheetLayout& l, float x, float y) {
    for (int i = 0; i < int(l.buttons.size()); ++i) {
        const D2D1_RECT_F& b = l.buttons[std::size_t(i)];
        if (x >= b.left && x < b.right && y >= b.top && y < b.bottom) return i;
    }
    return -1;
}

}  // namespace md::ui
