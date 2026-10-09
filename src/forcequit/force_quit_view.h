// Forcer à quitter (plan 54) : mise en page et dessin de la fenêtre « Forcer des applications à quitter » de macOS 26,
// en points (la cible porte l'échelle). Sans fenêtre ni état : la fenêtre (force_quit_window) et les tests s'en servent.
#pragma once
#include <d2d1.h>

#include <string>
#include <vector>

#include "../ui/ui_draw.h"
#include "force_quit_logic.h"

namespace md {

constexpr float kFqWidth = 380, kFqHeight = 420;   // taille de la fenêtre
constexpr float kFqTitleBar = 38, kFqMargin = 20;
constexpr float kFqRow = 26, kFqIcon = 20, kFqListInset = 5, kFqListRadius = 8, kFqRowRadius = 5;
constexpr float kFqFontBody = 13, kFqFontHint = 11;

extern const wchar_t* const kFqTitle;    // « Forcer des applications à quitter »
extern const wchar_t* const kFqIntro;    // « Si une app ne répond pas pendant un moment, sélectionnez son nom… »
extern const wchar_t* const kFqNotResponding;   // « (ne répond pas) »

struct FqRow {
    std::wstring name;
    bool hung = false;
    ID2D1Bitmap* icon = nullptr;   // nul : une case discrète à sa place
};

// Ce qui est dessiné.
struct FqView {
    std::vector<FqRow> rows;
    int selected = -1;
    float scroll = 0;                              // défilement de la liste, en points
    bool active = true;                            // fenêtre au premier plan : sélection sur l'accent, pastilles en couleur
    bool lightsHover = false;
    int lightPressed = -1;
    std::wstring buttonLabel = L"Forcer à quitter";   // « Relancer » pour l'Explorateur
    bool buttonEnabled = false, buttonPressed = false;
    std::wstring hint;                             // « Vous pouvez ouvrir cette fenêtre en appuyant sur ⌥⊞⎋. » ; vide : rien
};

struct FqLayout {
    D2D1_POINT_2F lights{};   // centre de la première pastille
    D2D1_RECT_F title{}, intro{}, list{}, hint{}, button{};
};
FqLayout layoutForceQuit(ui::Painter& p, const FqView& v);

D2D1_RECT_F rowRect(const FqLayout& l, int i, float scroll);
int rowAt(const FqLayout& l, std::size_t rows, float scroll, float x, float y);   // ligne sous le point, ou -1
float maxScroll(const FqLayout& l, std::size_t rows);
float scrollToShow(const FqLayout& l, std::size_t rows, float scroll, int i);      // défilement qui montre la ligne i
int lightAt(const FqLayout& l, float x, float y);   // 0 fermer, 1 réduire ; -1 ailleurs (agrandir est grisée)

void drawForceQuit(ui::Painter& p, const FqLayout& l, const FqView& v);

// Liste de la fenêtre (apps, sélection, défilement), sans fenêtre. La sélection est montrée dès que la mise en page est
// connue : à la première ouverture, la liste est lue (l'app du premier plan choisie) avant la première mise en page.
struct FqListState {
    std::vector<FqApp> apps;
    int selected = -1;
    float scroll = 0;
    std::wstring preselect;   // appId à choisir à la première liste non vide
    bool reveal = false;      // montrer la sélection à la prochaine mise en page

    void reload(std::vector<FqApp> next);   // l'app choisie suit sa ligne ; partie, la suivante (selectionAfter)
    void select(int i);                     // bornée à la liste
    void wheel(float points, const FqLayout& l);
    void afterLayout(const FqLayout& l);    // sélection montrée, défilement borné
};

// Feuille de confirmation : « Voulez-vous forcer « X » à quitter ? » [Annuler] [Forcer à quitter], ou « Relancer » pour
// l'Explorateur ; `alsoQuitting` : les autres apps qui partagent son processus, nommées (elles quitteront aussi).
ui::SheetSpec forceQuitConfirm(const FqApp& app, const std::vector<std::wstring>& alsoQuitting);

} // namespace md
