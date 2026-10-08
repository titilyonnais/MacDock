// Mod Windhawk « MacDock - macOS Look » : polices remplacées (logique) et crochets GDI / DirectWrite branchés sur
// les vraies fonctions de Windows dans ce processus (sans Windhawk : ses fonctions sont simulées ci-dessous).
#include <windows.h>
#include <dwrite.h>

#include <string>

#include "minitest.h"

namespace {
PCWSTR Wh_GetStringSetting(PCWSTR, ...) { return L""; }   // réglages par défaut du mod
void Wh_FreeStringSetting(PCWSTR) {}
int Wh_GetIntSetting(PCWSTR, ...) { return 1; }
void Wh_Log(PCWSTR, ...) {}
BOOL Wh_SetFunctionHook(void*, void*, void**) { return TRUE; }
} // namespace

#define Wh_ModInit lookModInit   // le mod de la barre des tâches est aussi inclus dans les tests
#define Wh_ModSettingsChanged lookModSettingsChanged
#include "../windhawk/macdock-look.wh.cpp"

TEST_CASE(look_replaces_windows_ui_fonts_only) {
    CHECK(look::mappingFor(L"Segoe UI").role == look::Role::Text);
    CHECK(look::mappingFor(L"segoe ui").role == look::Role::Text);   // sans casse
    CHECK(look::mappingFor(L"Segoe UI Variable Display").role == look::Role::Display);
    CHECK(look::mappingFor(L"MS Shell Dlg 2").role == look::Role::Text);
    // Tahoma et Microsoft Sans Serif servent aussi dans les documents (Word, PDF) : laissées.
    CHECK(look::mappingFor(L"Tahoma").role == look::Role::None);
    CHECK(look::mappingFor(L"Microsoft Sans Serif").role == look::Role::None);
    CHECK_EQ(look::mappingFor(L"Segoe UI Semibold").weight, 600);
    // Polices d'icônes et emoji : jamais.
    CHECK(look::mappingFor(L"Segoe Fluent Icons").role == look::Role::None);
    CHECK(look::mappingFor(L"Segoe MDL2 Assets").role == look::Role::None);
    CHECK(look::mappingFor(L"Segoe UI Emoji").role == look::Role::None);
    CHECK(look::mappingFor(L"Segoe UI Symbol").role == look::Role::None);
    CHECK(look::mappingFor(L"Consolas").role == look::Role::None);
    CHECK(look::mappingFor(nullptr).role == look::Role::None);
}

TEST_CASE(look_gdi_points_follow_dpi) {
    // Hauteur GDI en pixels du contexte DPI de l'appelant : 10 pt font 27 px à 200 %, pas 20 pt.
    CHECK(look::pointsForHeight(-27, 192) < 11);
    CHECK(look::pointsForHeight(-27, 96) > 19);
    CHECK(look::pointsForHeight(0, 96) == 0);
    CHECK(look::pointsForHeight(40, 96) < 30);   // positive : hauteur de cellule, un peu plus que les caractères
}

TEST_CASE(look_display_font_for_large_text) {
    // Comme macOS : SF Pro Display à partir de 20 pt.
    CHECK(look::roleForSize(look::Role::Text, 12) == look::Role::Text);
    CHECK(look::roleForSize(look::Role::Text, 20) == look::Role::Display);
    CHECK(look::roleForSize(look::Role::Text, 0) == look::Role::Text);
    CHECK(look::roleForSize(look::Role::None, 30) == look::Role::None);
}

TEST_CASE(look_hooks_on_real_gdi_and_directwrite) {
    loadSettings();
    if (!g_textAvailable) return;   // SF Pro absente de cette machine : le mod ne fait rien, rien à vérifier
    // GDI : « Segoe UI » demandée, SF Pro Text obtenue ; une police d'icônes reste elle-même.
    HMODULE gdi = LoadLibraryW(L"gdi32full.dll");
    REQUIRE(gdi != nullptr);
    CreateFontIndirectExW_Original = reinterpret_cast<CreateFontIndirectExW_t>(GetProcAddress(gdi, "CreateFontIndirectExW"));
    REQUIRE(CreateFontIndirectExW_Original != nullptr);
    auto faceOf = [](const wchar_t* face) {
        ENUMLOGFONTEXDVW e{};
        e.elfEnumLogfontEx.elfLogFont.lfHeight = -16;
        e.elfEnumLogfontEx.elfLogFont.lfCharSet = DEFAULT_CHARSET;
        wcsncpy_s(e.elfEnumLogfontEx.elfLogFont.lfFaceName, face, _TRUNCATE);
        HFONT f = CreateFontIndirectExW_Hook(&e);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[LF_FACESIZE] = {};
        GetTextFaceW(dc, LF_FACESIZE, got);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(f);
        return std::wstring(got);
    };
    CHECK(faceOf(L"Segoe UI") == L"SF Pro Text");
    CHECK(faceOf(L"Consolas") == L"Consolas");
    // DirectWrite : la collection système trouve SF Pro Text sous le nom « Segoe UI » ; un grand titre prend Display.
    IDWriteFactory* factory = nullptr;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                          reinterpret_cast<IUnknown**>(&factory))));
    IDWriteFontCollection* system = nullptr;
    REQUIRE(SUCCEEDED(factory->GetSystemFontCollection(&system, FALSE)));
    FindFamilyName_Original = reinterpret_cast<FindFamilyName_t>((*reinterpret_cast<void***>(system))[5]);
    CreateTextFormat_Original = reinterpret_cast<CreateTextFormat_t>((*reinterpret_cast<void***>(factory))[15]);
    UINT32 index = 0, sfIndex = 0;
    BOOL exists = FALSE, sfExists = FALSE;
    REQUIRE(SUCCEEDED(FindFamilyName_Hook(system, L"Segoe UI", &index, &exists)));
    REQUIRE(SUCCEEDED(system->FindFamilyName(L"SF Pro Text", &sfIndex, &sfExists)));
    CHECK(exists && sfExists && index == sfIndex);
    IDWriteTextFormat* format = nullptr;
    REQUIRE(SUCCEEDED(CreateTextFormat_Hook(factory, L"Segoe UI Variable", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 32.0f, L"fr-FR", &format)));
    wchar_t family[64] = {};
    format->GetFontFamilyName(family, 64);
    CHECK(std::wstring(family) == (g_displayAvailable ? L"SF Pro Display" : L"SF Pro Text"));   // 32 DIP = 24 pt
    format->Release();
    system->Release();
    factory->Release();
}
