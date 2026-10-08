#include "pane_icons.h"

#include <cmath>

namespace md {

using Microsoft::WRL::ComPtr;

namespace {

// Repère de la tuile : une case de 20 × 20 unités, quelle que soit sa taille réelle.
struct Box {
    D2D1_RECT_F r;
    float k;
    D2D1_POINT_2F at(float x, float y) const { return D2D1::Point2F(r.left + x * k, r.top + y * k); }
    D2D1_RECT_F rect(float l, float t, float rr, float b) const {
        return D2D1::RectF(r.left + l * k, r.top + t * k, r.left + rr * k, r.top + b * k);
    }
};

void line(ui::Painter& p, const Box& b, float x0, float y0, float x1, float y1, float w) {
    p.rt()->DrawLine(b.at(x0, y0), b.at(x1, y1), p.brush(ui::rgb(0xFFFFFF)), w * b.k);
}

void icon(ui::Painter& p, const Box& b, PaneIcon which) {
    const ui::Rgba white = ui::rgb(0xFFFFFF);
    ID2D1RenderTarget* rt = p.rt();
    switch (which) {
        case PaneIcon::Gear: {
            rt->DrawEllipse(D2D1::Ellipse(b.at(10, 10), 3.6f * b.k, 3.6f * b.k), p.brush(white), 2.2f * b.k);
            for (int i = 0; i < 8; ++i) {
                const float a = float(i) * 3.14159265f / 4, c = std::cos(a), s = std::sin(a);
                line(p, b, 10 + 4.8f * c, 10 + 4.8f * s, 10 + 7 * c, 10 + 7 * s, 2.4f);
            }
            break;
        }
        case PaneIcon::Dock:
            p.fillRound(b.rect(3, 12, 17, 16), 1.5f * b.k, white);
            for (float x : {4.5f, 8.5f, 12.5f}) p.fillRound(b.rect(x, 7, x + 3, 10), 0.8f * b.k, white);
            break;
        case PaneIcon::MenuBar:
            p.strokeRound(b.rect(3.5f, 4.5f, 16.5f, 15.5f), 2 * b.k, white, 1.4f * b.k);
            p.fillRound(b.rect(3.5f, 4.5f, 16.5f, 8), 1.5f * b.k, white);
            break;
        case PaneIcon::Windows:
            p.strokeRound(b.rect(3, 3.5f, 13, 11.5f), 1.6f * b.k, white, 1.3f * b.k);
            p.fillRound(b.rect(7, 8.5f, 17, 16.5f), 1.6f * b.k, white);
            break;
        case PaneIcon::Desktop:
            for (float x : {3.5f, 10.5f})
                for (float y : {4.5f, 10.5f}) p.fillRound(b.rect(x, y, x + 6, y + 5), 1.2f * b.k, white);
            break;
        case PaneIcon::Keyboard:
            p.strokeRound(b.rect(2.5f, 5.5f, 17.5f, 14.5f), 1.6f * b.k, white, 1.3f * b.k);
            for (float y : {8.f, 10.6f})
                for (float x = 5; x < 15.5f; x += 2.5f) p.fillRound(b.rect(x - 0.7f, y - 0.7f, x + 0.7f, y + 0.7f), 0.3f * b.k, white);
            line(p, b, 7, 12.7f, 13, 12.7f, 1.3f);
            break;
        case PaneIcon::Screenshot:
            for (float sx : {-1.f, 1.f})
                for (float sy : {-1.f, 1.f}) {
                    const float x = 10 + sx * 6.5f, y = 10 + sy * 5.5f;
                    line(p, b, x, y, x - sx * 3.5f, y, 1.6f);
                    line(p, b, x, y, x, y - sy * 3.5f, 1.6f);
                }
            p.fillCircle(b.at(10, 10), 2 * b.k, white);
            break;
        case PaneIcon::Sound: {
            ComPtr<ID2D1PathGeometry> g;
            ComPtr<ID2D1GeometrySink> s;
            if (SUCCEEDED(p.factory()->CreatePathGeometry(&g)) && SUCCEEDED(g->Open(&s))) {
                s->BeginFigure(b.at(3.5f, 8), D2D1_FIGURE_BEGIN_FILLED);
                const D2D1_POINT_2F pts[] = {b.at(6.5f, 8), b.at(10, 4.5f), b.at(10, 15.5f), b.at(6.5f, 12), b.at(3.5f, 12)};
                s->AddLines(pts, 5);
                s->EndFigure(D2D1_FIGURE_END_CLOSED);
                s->Close();
                rt->FillGeometry(g.Get(), p.brush(white));
            }
            for (float radius : {3.f, 5.5f}) {   // ondes
                ComPtr<ID2D1PathGeometry> a;
                ComPtr<ID2D1GeometrySink> as;
                if (FAILED(p.factory()->CreatePathGeometry(&a)) || FAILED(a->Open(&as))) continue;
                as->BeginFigure(b.at(11 + radius * 0.5f, 10 - radius * 0.866f), D2D1_FIGURE_BEGIN_HOLLOW);
                as->AddArc(D2D1::ArcSegment(b.at(11 + radius * 0.5f, 10 + radius * 0.866f), D2D1::SizeF(radius * b.k, radius * b.k), 0,
                                            D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
                as->EndFigure(D2D1_FIGURE_END_OPEN);
                as->Close();
                rt->DrawGeometry(a.Get(), p.brush(white), 1.4f * b.k);
            }
            break;
        }
        case PaneIcon::Font:
            p.text(L"Aa", b.rect(0, 0, 20, 20), 10.5f * b.k, white, DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT_CENTER);
            break;
        case PaneIcon::Puzzle:
            p.fillRound(b.rect(4, 6, 13, 15), 1.5f * b.k, white);
            p.fillCircle(b.at(8.5f, 6), 2.3f * b.k, white);
            p.fillCircle(b.at(13, 10.5f), 2.3f * b.k, white);
            break;
        case PaneIcon::Info:
            rt->DrawEllipse(D2D1::Ellipse(b.at(10, 10), 6.5f * b.k, 6.5f * b.k), p.brush(white), 1.4f * b.k);
            p.fillCircle(b.at(10, 6.6f), 1.1f * b.k, white);
            line(p, b, 10, 9, 10, 14, 1.8f);
            break;
    }
}

}  // namespace

void drawPaneTile(ui::Painter& p, D2D1_RECT_F r, std::uint32_t color, PaneIcon which) {
    const Box b{r, (r.right - r.left) / 20.f};
    const ui::Rgba base = ui::rgb(color);
    // Léger dégradé vertical : un peu plus clair en haut, comme les tuiles des Réglages Système.
    ComPtr<ID2D1GradientStopCollection> stops;
    const float o = p.opacity;
    const D2D1_GRADIENT_STOP gs[2] = {
        {0, D2D1::ColorF(std::min(1.f, base.r + 0.08f), std::min(1.f, base.g + 0.08f), std::min(1.f, base.b + 0.08f), o)},
        {1, D2D1::ColorF(base.r * 0.92f, base.g * 0.92f, base.b * 0.92f, o)}};
    ComPtr<ID2D1LinearGradientBrush> gradient;
    if (SUCCEEDED(p.rt()->CreateGradientStopCollection(gs, 2, &stops)) &&
        SUCCEEDED(p.rt()->CreateLinearGradientBrush(D2D1::LinearGradientBrushProperties(D2D1::Point2F(r.left, r.top),
                                                                                         D2D1::Point2F(r.left, r.bottom)),
                                                    stops.Get(), &gradient))) {
        const float radius = ui::metrics::tileRadius * b.k;
        p.rt()->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), gradient.Get());
    } else {
        p.fillRound(r, ui::metrics::tileRadius * b.k, base);
    }
    icon(p, b, which);
}

}  // namespace md
