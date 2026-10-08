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

void Painter::paragraph(const std::wstring& s, D2D1_RECT_F r, float size, Rgba c) {
    if (s.empty()) return;
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dwrite_->CreateTextFormat(font_.c_str(), nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL,
                                         DWRITE_FONT_STRETCH_NORMAL, size, L"fr-FR", &f)))
        return;
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    rt_->DrawText(s.c_str(), UINT32(s.size()), f.Get(), r, brush(c));
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
    // Teintes des pastilles du projet (Golden Gate, voir traffic_lights.cpp) : remplissage et liseré.
    static constexpr std::uint32_t kFill[3] = {0xE26E65, 0xF0BE5E, 0x68C05D}, kEdge[3] = {0xC4483F, 0xD29C38, 0x3E9C3A};
    const bool dark = darkPalette(p.pal());
    for (int i = 0; i < 3; ++i) {
        const D2D1_POINT_2F c{first.x + i * kLightSpacing, first.y};
        const bool gray = !active || i == disabled;
        Rgba fill = gray ? rgb(dark ? 0x4E4F52 : 0xDDDDDD) : rgb(kFill[i]);
        const Rgba edge = gray ? rgb(dark ? 0x3E3F42 : 0xC4C3C6) : rgb(kEdge[i]);
        if (i == pressed && !gray) fill = mix(fill, rgb(0x000000), 0.22f);
        p.fillCircle(c, kLightRadius, edge);
        p.fillCircle(c, kLightRadius - 0.6f, fill);
        if (!gray)   // reflet du haut
            p.rt()->FillEllipse(D2D1::Ellipse(D2D1::Point2F(c.x, c.y - 3.2f), 4.2f, 2.3f), p.brush(rgb(0xFFFFFF, 0.28f)));
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

}  // namespace md::ui
