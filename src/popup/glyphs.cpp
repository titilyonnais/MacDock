#include "glyphs.h"

#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace md {
namespace {

using Microsoft::WRL::ComPtr;
constexpr float kPi = 3.14159265f;

// Repère du pictogramme : coordonnées en fractions de la boîte (0..1).
struct Frame {
    D2D1_RECT_F box;
    float w() const { return box.right - box.left; }
    float h() const { return box.bottom - box.top; }
    float s() const { return std::min(w(), h()); }
    D2D1_POINT_2F p(float x, float y) const {
        const float side = s();
        const float ox = box.left + (w() - side) / 2, oy = box.top + (h() - side) / 2;
        return {ox + x * side, oy + y * side};
    }
    float len(float f) const { return f * s(); }
};

ComPtr<ID2D1PathGeometry> path(ID2D1RenderTarget* rt) {
    ComPtr<ID2D1Factory> f;
    rt->GetFactory(&f);
    ComPtr<ID2D1PathGeometry> g;
    f->CreatePathGeometry(&g);
    return g;
}

void polygon(ID2D1RenderTarget* rt, const Frame& f, std::initializer_list<D2D1_POINT_2F> pts, ID2D1Brush* ink) {
    auto g = path(rt);
    ComPtr<ID2D1GeometrySink> sink;
    if (!g || FAILED(g->Open(&sink))) return;
    auto it = pts.begin();
    sink->BeginFigure(f.p(it->x, it->y), D2D1_FIGURE_BEGIN_FILLED);
    for (++it; it != pts.end(); ++it) sink->AddLine(f.p(it->x, it->y));
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    rt->FillGeometry(g.Get(), ink);
}

// Arc de cercle de centre c, rayon r (fractions), de a0 à a1 (radians, 0 = droite, sens horaire à l'écran).
void arc(ID2D1RenderTarget* rt, const Frame& f, D2D1_POINT_2F c, float r, float a0, float a1, ID2D1Brush* ink, float width) {
    auto g = path(rt);
    ComPtr<ID2D1GeometrySink> sink;
    if (!g || FAILED(g->Open(&sink))) return;
    sink->BeginFigure(f.p(c.x + r * std::cos(a0), c.y + r * std::sin(a0)), D2D1_FIGURE_BEGIN_HOLLOW);
    sink->AddArc(D2D1::ArcSegment(f.p(c.x + r * std::cos(a1), c.y + r * std::sin(a1)), D2D1::SizeF(f.len(r), f.len(r)), 0,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE, std::fabs(a1 - a0) > kPi ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
    sink->EndFigure(D2D1_FIGURE_END_OPEN);
    sink->Close();
    ComPtr<ID2D1Factory> fac;
    rt->GetFactory(&fac);
    ComPtr<ID2D1StrokeStyle> round;
    fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0, &round);
    rt->DrawGeometry(g.Get(), ink, width, round.Get());
}

void line(ID2D1RenderTarget* rt, const Frame& f, float x0, float y0, float x1, float y1, ID2D1Brush* ink, float width) {
    ComPtr<ID2D1Factory> fac;
    rt->GetFactory(&fac);
    ComPtr<ID2D1StrokeStyle> round;
    fac->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                                       D2D1_LINE_JOIN_ROUND),
                           nullptr, 0, &round);
    rt->DrawLine(f.p(x0, y0), f.p(x1, y1), ink, width, round.Get());
}

D2D1_ROUNDED_RECT rrect(const Frame& f, float x0, float y0, float x1, float y1, float r) {
    const auto a = f.p(x0, y0), b = f.p(x1, y1);
    return D2D1::RoundedRect(D2D1::RectF(a.x, a.y, b.x, b.y), f.len(r), f.len(r));
}

// Opacité réduite pour les parties éteintes, rétablie en sortie.
struct Dim {
    ID2D1Brush* ink;
    float saved;
    Dim(ID2D1Brush* b, bool dim) : ink(b), saved(b->GetOpacity()) {
        if (dim) b->SetOpacity(saved * 0.3f);
    }
    ~Dim() { ink->SetOpacity(saved); }
};

void speaker(ID2D1RenderTarget* rt, const Frame& f, ID2D1Brush* ink, float level, bool muted) {
    polygon(rt, f, {{0.06f, 0.38f}, {0.24f, 0.38f}, {0.46f, 0.18f}, {0.46f, 0.82f}, {0.24f, 0.62f}, {0.06f, 0.62f}}, ink);
    const float w = f.len(0.075f);
    if (muted) {
        line(rt, f, 0.60f, 0.36f, 0.88f, 0.64f, ink, w);
        line(rt, f, 0.88f, 0.36f, 0.60f, 0.64f, ink, w);
        return;
    }
    const int waves = level <= 0 ? 0 : level < 0.34f ? 1 : level < 0.67f ? 2 : 3;
    for (int i = 0; i < 3; ++i) {
        Dim dim(ink, i >= waves);
        const float r = 0.14f + 0.13f * float(i);
        arc(rt, f, {0.46f, 0.5f}, r, -0.9f, 0.9f, ink, w);
    }
}

void wifi(ID2D1RenderTarget* rt, const Frame& f, ID2D1Brush* ink, float level, bool off) {
    const float w = f.len(0.09f);
    const D2D1_POINT_2F c{0.5f, 0.82f};
    const int lit = off ? 0 : std::clamp(int(std::ceil(level * 3)), 0, 3);
    for (int i = 0; i < 3; ++i) {
        Dim dim(ink, i >= lit);
        arc(rt, f, c, 0.22f + 0.2f * float(i), -kPi * 0.78f, -kPi * 0.22f, ink, w);
    }
    {
        Dim dim(ink, off);
        rt->FillEllipse(D2D1::Ellipse(f.p(c.x, c.y), f.len(0.07f), f.len(0.07f)), ink);
    }
    if (off) line(rt, f, 0.15f, 0.15f, 0.85f, 0.85f, ink, w);
}

void battery(ID2D1RenderTarget* rt, const Frame& f, ID2D1Brush* ink, float level, bool charging) {
    const float w = f.len(0.06f);
    rt->DrawRoundedRectangle(rrect(f, 0.04f, 0.28f, 0.84f, 0.72f, 0.1f), ink, w);
    rt->FillRoundedRectangle(rrect(f, 0.87f, 0.42f, 0.94f, 0.58f, 0.03f), ink);
    const float fill = 0.10f + 0.68f * std::clamp(level, 0.0f, 1.0f);
    const D2D1_ROUNDED_RECT bar = rrect(f, 0.10f, 0.34f, std::max(0.12f, fill), 0.66f, 0.05f);
    if (!charging) {
        rt->FillRoundedRectangle(bar, ink);
        return;
    }
    // En charge : l'éclair est en creux dans la charge et en plein ailleurs, comme sur macOS.
    ComPtr<ID2D1Factory> fac;
    rt->GetFactory(&fac);
    auto bolt = path(rt);
    ComPtr<ID2D1GeometrySink> sink;
    if (!bolt || FAILED(bolt->Open(&sink))) return;
    const D2D1_POINT_2F pts[] = {f.p(0.50f, 0.18f), f.p(0.30f, 0.54f), f.p(0.45f, 0.54f),
                                 f.p(0.39f, 0.82f), f.p(0.61f, 0.44f), f.p(0.46f, 0.44f)};
    sink->BeginFigure(pts[0], D2D1_FIGURE_BEGIN_FILLED);
    sink->AddLines(pts + 1, 5);
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    ComPtr<ID2D1RoundedRectangleGeometry> charge;
    fac->CreateRoundedRectangleGeometry(bar, &charge);
    if (!charge) return;
    ComPtr<ID2D1PathGeometry> hole = path(rt), outside = path(rt);
    ComPtr<ID2D1GeometrySink> s1, s2;
    if (hole && SUCCEEDED(hole->Open(&s1))) {
        charge->CombineWithGeometry(bolt.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, s1.Get());
        s1->Close();
        rt->FillGeometry(hole.Get(), ink);
    }
    if (outside && SUCCEEDED(outside->Open(&s2))) {
        bolt->CombineWithGeometry(charge.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, s2.Get());
        s2->Close();
        rt->FillGeometry(outside.Get(), ink);
    }
}

// Deux interrupteurs, comme le Centre de contrôle de la barre de macOS 26 (cotes ajustées sur une capture de la VM, en
// points d'une case de 16 centrée sur la barre de 24) : en haut, piste creuse et bouton plein à gauche ; en bas, piste
// pleine et bouton creux à droite.
void controlCenter(ID2D1RenderTarget* rt, const Frame& f, ID2D1Brush* ink) {
    auto q = [](float pt) { return pt / 16; };
    auto pill = [&](float l, float t, float r, float b) { return rrect(f, q(l), q(t), q(r), q(b), q((b - t) / 2)); };
    const float left = 1.403f, right = 14.597f;
    // Piste du haut (6,48 de haut) : contour de 1,27 à l'intérieur ; bouton plein, à 0,48 du contour.
    const float top = 1.688f, mid = 8.165f, stroke = 1.267f, gap = 0.482f;
    rt->DrawRoundedRectangle(pill(left + stroke / 2, top + stroke / 2, right - stroke / 2, mid - stroke / 2), ink, f.len(q(stroke)));
    rt->FillRoundedRectangle(pill(left + stroke + gap, top + stroke + gap, left + 6.836f, mid - stroke - gap), ink);
    // Piste du bas (5,80 de haut), pleine ; bouton creux à 1,21 de son bord.
    const float low = 9.186f, bottom = 14.985f, edge = 1.212f;
    ComPtr<ID2D1Factory> fac;
    rt->GetFactory(&fac);
    ComPtr<ID2D1RoundedRectangleGeometry> track, knob;
    fac->CreateRoundedRectangleGeometry(pill(left, low, right, bottom), &track);
    fac->CreateRoundedRectangleGeometry(pill(left + 6.515f, low + edge, right - edge, bottom - edge), &knob);
    auto g = path(rt);
    ComPtr<ID2D1GeometrySink> sink;
    if (track && knob && g && SUCCEEDED(g->Open(&sink))) {
        track->CombineWithGeometry(knob.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
        sink->Close();
        rt->FillGeometry(g.Get(), ink);
    }
}

} // namespace

void drawGlyph(ID2D1RenderTarget* rt, Glyph g, D2D1_RECT_F box, ID2D1Brush* ink, float level, bool alt) {
    if (!rt || !ink) return;
    const Frame f{box};
    const float w = f.len(0.08f);
    switch (g) {
        case Glyph::None: break;
        case Glyph::Speaker: speaker(rt, f, ink, level, alt); break;
        case Glyph::Sun: {
            rt->FillEllipse(D2D1::Ellipse(f.p(0.5f, 0.5f), f.len(0.17f), f.len(0.17f)), ink);
            for (int i = 0; i < 8; ++i) {
                const float a = float(i) * kPi / 4, c = std::cos(a), s = std::sin(a);
                line(rt, f, 0.5f + 0.29f * c, 0.5f + 0.29f * s, 0.5f + 0.42f * c, 0.5f + 0.42f * s, ink, w);
            }
            break;
        }
        case Glyph::Wifi: wifi(rt, f, ink, level, alt); break;
        case Glyph::Ethernet:
            rt->DrawRoundedRectangle(rrect(f, 0.36f, 0.08f, 0.64f, 0.34f, 0.04f), ink, w);
            rt->DrawRoundedRectangle(rrect(f, 0.06f, 0.66f, 0.34f, 0.92f, 0.04f), ink, w);
            rt->DrawRoundedRectangle(rrect(f, 0.66f, 0.66f, 0.94f, 0.92f, 0.04f), ink, w);
            line(rt, f, 0.5f, 0.34f, 0.5f, 0.5f, ink, w);
            line(rt, f, 0.2f, 0.5f, 0.8f, 0.5f, ink, w);
            line(rt, f, 0.2f, 0.5f, 0.2f, 0.66f, ink, w);
            line(rt, f, 0.8f, 0.5f, 0.8f, 0.66f, ink, w);
            break;
        case Glyph::Bluetooth:
            line(rt, f, 0.28f, 0.30f, 0.70f, 0.68f, ink, w);
            line(rt, f, 0.28f, 0.70f, 0.70f, 0.32f, ink, w);
            line(rt, f, 0.70f, 0.32f, 0.49f, 0.12f, ink, w);
            line(rt, f, 0.49f, 0.12f, 0.49f, 0.88f, ink, w);
            line(rt, f, 0.49f, 0.88f, 0.70f, 0.68f, ink, w);
            break;
        case Glyph::Moon: {
            ComPtr<ID2D1Factory> fac;
            rt->GetFactory(&fac);
            ComPtr<ID2D1EllipseGeometry> a, b;
            fac->CreateEllipseGeometry(D2D1::Ellipse(f.p(0.48f, 0.52f), f.len(0.36f), f.len(0.36f)), &a);
            fac->CreateEllipseGeometry(D2D1::Ellipse(f.p(0.68f, 0.36f), f.len(0.30f), f.len(0.30f)), &b);
            auto g2 = path(rt);
            ComPtr<ID2D1GeometrySink> sink;
            if (a && b && g2 && SUCCEEDED(g2->Open(&sink))) {
                a->CombineWithGeometry(b.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
                sink->Close();
                rt->FillGeometry(g2.Get(), ink);
            }
            break;
        }
        case Glyph::ScreenMirror:
            rt->DrawRoundedRectangle(rrect(f, 0.08f, 0.14f, 0.92f, 0.70f, 0.08f), ink, w);
            polygon(rt, f, {{0.5f, 0.56f}, {0.74f, 0.90f}, {0.26f, 0.90f}}, ink);
            break;
        case Glyph::Battery: battery(rt, f, ink, level, alt); break;
        case Glyph::Search: {
            // Loupe de la barre de macOS 26 (cotes ajustées sur une capture de la VM, en points d'une case de 16) :
            // anneau de 4,50 de rayon au trait de 1,32 ; manche à 45,6°, jusqu'à 9,16 du centre, au trait de 1,90.
            const float cx = 6.905f / 16, cy = 7.525f / 16, r = 4.503f / 16, end = 9.157f / 16;
            const float a = 45.64f * kPi / 180, ca = std::cos(a), sa = std::sin(a);
            rt->DrawEllipse(D2D1::Ellipse(f.p(cx, cy), f.len(r), f.len(r)), ink, f.len(1.322f / 16));
            line(rt, f, cx + r * ca, cy + r * sa, cx + end * ca, cy + end * sa, ink, f.len(1.898f / 16));
            break;
        }
        case Glyph::ControlCenter: controlCenter(rt, f, ink); break;
        case Glyph::Play: polygon(rt, f, {{0.28f, 0.16f}, {0.82f, 0.5f}, {0.28f, 0.84f}}, ink); break;
        case Glyph::Pause:
            rt->FillRoundedRectangle(rrect(f, 0.24f, 0.16f, 0.42f, 0.84f, 0.04f), ink);
            rt->FillRoundedRectangle(rrect(f, 0.58f, 0.16f, 0.76f, 0.84f, 0.04f), ink);
            break;
        case Glyph::Previous:
            polygon(rt, f, {{0.50f, 0.22f}, {0.50f, 0.78f}, {0.10f, 0.50f}}, ink);
            polygon(rt, f, {{0.90f, 0.22f}, {0.90f, 0.78f}, {0.50f, 0.50f}}, ink);
            break;
        case Glyph::Next:
            polygon(rt, f, {{0.10f, 0.22f}, {0.50f, 0.50f}, {0.10f, 0.78f}}, ink);
            polygon(rt, f, {{0.50f, 0.22f}, {0.90f, 0.50f}, {0.50f, 0.78f}}, ink);
            break;
    }
}

} // namespace md
