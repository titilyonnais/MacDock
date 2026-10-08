// Dessin des contrôles de l'app Réglages en Direct2D, façon macOS 27 : interrupteur, curseur, menu déroulant, contrôle
// segmenté, groupes arrondis, menu ouvert, champ de recherche, pastilles de la fenêtre. Coordonnées en points : la cible
// porte l'échelle (DPI). Aucune ressource Apple : tout est tracé ici.
#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

#include <string>
#include <vector>

#include "ui_theme.h"

namespace md::ui {

// Pinceau partagé, formats de texte en cache et quelques primitives ; `opacity` atténue tout (contrôle grisé).
class Painter {
public:
    Painter(ID2D1RenderTarget* rt, IDWriteFactory* dwrite, const Palette& palette, std::wstring font);
    ID2D1RenderTarget* rt() const { return rt_; }
    ID2D1Factory* factory() const;
    const Palette& pal() const { return pal_; }
    float opacity = 1;

    ID2D1SolidColorBrush* brush(Rgba c);
    IDWriteTextFormat* format(float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR,
                              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    void fillRound(D2D1_RECT_F r, float radius, Rgba c);
    void strokeRound(D2D1_RECT_F r, float radius, Rgba c, float width);
    void fillCircle(D2D1_POINT_2F c, float radius, Rgba color);
    // Texte sur une ligne, centré verticalement dans r ; coupé par des points de suspension s'il déborde.
    void text(const std::wstring& s, D2D1_RECT_F r, float size, Rgba c, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR,
              DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING);
    // Texte sur plusieurs lignes, depuis le haut de r.
    void paragraph(const std::wstring& s, D2D1_RECT_F r, float size, Rgba c);
    float textWidth(const std::wstring& s, float size, DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_REGULAR);
    const std::wstring& font() const { return font_; }

private:
    struct Format {
        float size;
        DWRITE_FONT_WEIGHT weight;
        DWRITE_TEXT_ALIGNMENT align;
        Microsoft::WRL::ComPtr<IDWriteTextFormat> format;
    };
    ID2D1RenderTarget* rt_;
    IDWriteFactory* dwrite_;
    const Palette& pal_;
    std::wstring font_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush_;
    std::vector<Format> formats_;
};

// Police de l'interface : SF Pro si elle est installée, puis Inter, puis Segoe UI Variable.
std::wstring interfaceFont(IDWriteFactory* dwrite);

// Interrupteur dans r (32 × 18 pt) ; progress de 0 (éteint) à 1 (allumé), animé par l'appelant.
void drawSwitch(Painter& p, D2D1_RECT_F r, float progress, bool pressed);
// Curseur : piste de left à right sur la ligne cy, bouton à t (0 à 1).
void drawSlider(Painter& p, float left, float right, float cy, float t, bool pressed);
// Menu déroulant fermé : texte et double chevron, sur un fond discret.
void drawPopup(Painter& p, D2D1_RECT_F r, const std::wstring& text, bool pressed);
float popupWidth(Painter& p, const std::wstring& text);
// Contrôle segmenté de segments égaux ; `selected` sur un fond clair en relief.
void drawSegmented(Painter& p, D2D1_RECT_F r, const std::vector<std::wstring>& labels, int selected);
float segmentedWidth(Painter& p, const std::vector<std::wstring>& labels);
// Menu ouvert : `checked` coché, `hover` sur l'accent ; éléments de metrics::menuItem.
void drawMenu(Painter& p, D2D1_RECT_F r, const std::vector<std::wstring>& items, int checked, int hover);
float menuWidth(Painter& p, const std::vector<std::wstring>& items);
// Champ de recherche en capsule, loupe et texte (ou « Rechercher » en gris).
void drawSearchField(Painter& p, D2D1_RECT_F r, const std::wstring& text, bool focused);
// Anneau de focus autour de r.
void drawFocusRing(Painter& p, D2D1_RECT_F r, float radius);
// Pastilles de la fenêtre (fermer, réduire, agrandir), le premier centre en `first` ; grises si inactive ; symboles au
// survol ; `disabled` : indice grisé (agrandir), -1 sinon.
void drawWindowLights(Painter& p, D2D1_POINT_2F first, bool active, bool hover, int pressed, int disabled = -1);
constexpr float kLightRadius = 7, kLightSpacing = 23;

}  // namespace md::ui
