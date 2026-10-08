// Mod Windhawk « MacDock - macOS Look » : polices remplacées (logique) et crochets GDI / DirectWrite branchés sur
// les vraies fonctions de Windows dans ce processus (sans Windhawk : ses fonctions sont simulées ci-dessous).
#include <windows.h>
#include <dwrite_3.h>

#include <cstdint>
#include <string>
#include <tuple>

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

TEST_CASE(look_xaml_variable_font_path_with_axes) {
    // XAML (Bloc-notes, Explorateur, Paramètres) demande Segoe UI Variable par IDWriteFactory6::CreateTextFormat, avec
    // ses axes (épaisseur, taille optique) : ce chemin doit aussi donner SF Pro.
    loadSettings();
    if (!g_textAvailable) return;
    IDWriteFactory6* f6 = nullptr;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6), reinterpret_cast<IUnknown**>(&f6))));
    CreateTextFormat6_Original = reinterpret_cast<CreateTextFormat6_t>((*reinterpret_cast<void***>(f6))[54]);
    DWRITE_FONT_AXIS_VALUE axes[] = {{DWRITE_FONT_AXIS_TAG_WEIGHT, 400}, {DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, 10.5f}};
    auto familyOf = [&](const wchar_t* name, float size) {
        IDWriteTextFormat3* f = nullptr;
        std::wstring got;
        if (SUCCEEDED(CreateTextFormat6_Hook(f6, name, nullptr, axes, 2, size, L"fr-FR", &f)) && f) {
            wchar_t buf[64] = {};
            f->GetFontFamilyName(buf, 64);
            got = buf;
            f->Release();
        }
        return got;
    };
    CHECK(familyOf(L"Segoe UI Variable", 14) == L"SF Pro Text");
    CHECK(familyOf(L"Segoe UI Variable Display", 40) == (g_displayAvailable ? L"SF Pro Display" : L"SF Pro Text"));
    CHECK(familyOf(L"Cascadia Code", 14) == L"Cascadia Code");   // le reste n'est jamais touché
    f6->Release();
}

namespace {
// Texte à analyser pour IDWriteFontFallback::MapCharacters (une seule langue, gauche à droite).
struct LookTextSource : IDWriteTextAnalysisSource {
    const wchar_t* text;
    UINT32 length;
    explicit LookTextSource(const wchar_t* t) : text(t), length(UINT32(wcslen(t))) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDWriteTextAnalysisSource)) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
    ULONG STDMETHODCALLTYPE Release() override { return 1; }
    HRESULT STDMETHODCALLTYPE GetTextAtPosition(UINT32 at, const WCHAR** t, UINT32* n) override {
        *t = at < length ? text + at : nullptr;
        *n = at < length ? length - at : 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetTextBeforePosition(UINT32 at, const WCHAR** t, UINT32* n) override {
        *t = at > 0 && at <= length ? text : nullptr;
        *n = at > 0 && at <= length ? at : 0;
        return S_OK;
    }
    DWRITE_READING_DIRECTION STDMETHODCALLTYPE GetParagraphReadingDirection() override { return DWRITE_READING_DIRECTION_LEFT_TO_RIGHT; }
    HRESULT STDMETHODCALLTYPE GetLocaleName(UINT32 at, UINT32* n, const WCHAR** locale) override {
        *n = length - at;
        *locale = L"fr-FR";
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetNumberSubstitution(UINT32 at, UINT32* n, IDWriteNumberSubstitution** s) override {
        *n = length - at;
        *s = nullptr;
        return S_OK;
    }
};

// Nom de famille Win32 (« SF Pro Text », « SF Pro Text Semibold »…) d'une police ou d'une face.
template <class T>
std::wstring win32Family(T* font) {
    IDWriteLocalizedStrings* names = nullptr;
    BOOL exists = FALSE;
    wchar_t buf[128] = {};
    if (font && SUCCEEDED(font->GetInformationalStrings(DWRITE_INFORMATIONAL_STRING_WIN32_FAMILY_NAMES, &names, &exists)) && exists)
        names->GetString(0, buf, 128);
    if (names) names->Release();
    return buf;
}

bool startsWith(const std::wstring& s, const wchar_t* prefix) { return s.rfind(prefix, 0) == 0; }
} // namespace

TEST_CASE(look_xaml_matching_fonts_with_axes) {
    // XAML choisit la police affichée par IDWriteFontCollection2::GetMatchingFonts(« Segoe UI Variable », axes) : ce
    // chemin doit donner SF Pro, Display à partir de 20 pt de taille optique, et la graisse du nom (« Semibold »).
    loadSettings();
    if (!g_textAvailable) return;
    IDWriteFactory6* f6 = nullptr;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory6), reinterpret_cast<IUnknown**>(&f6))));
    IDWriteFontCollection2* typo = nullptr;
    REQUIRE(SUCCEEDED(f6->GetSystemFontCollection(FALSE, DWRITE_FONT_FAMILY_MODEL_TYPOGRAPHIC, &typo)));
    GetMatchingFonts_Original = reinterpret_cast<GetMatchingFonts_t>((*reinterpret_cast<void***>(typo))[10]);
    struct Got {
        std::wstring family;
        int weight = 0;
    };
    auto firstMatch = [&](const wchar_t* name, float opsz, float wght) {
        DWRITE_FONT_AXIS_VALUE axes[] = {{DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, opsz}, {DWRITE_FONT_AXIS_TAG_WEIGHT, wght},
                                         {DWRITE_FONT_AXIS_TAG_ITALIC, 0}};
        Got got;
        IDWriteFontList2* list = nullptr;
        if (SUCCEEDED(GetMatchingFonts_Hook(typo, name, axes, 3, &list)) && list) {
            IDWriteFont* font = nullptr;
            if (list->GetFontCount() > 0 && SUCCEEDED(list->GetFont(0, &font)) && font) {
                got = {win32Family(font), int(font->GetWeight())};
                font->Release();
            }
            list->Release();
        }
        return got;
    };
    CHECK(startsWith(firstMatch(L"Segoe UI Variable", 10.5f, 400).family, L"SF Pro Text"));
    CHECK(startsWith(firstMatch(L"Segoe UI Variable Text", 10.5f, 400).family, L"SF Pro Text"));
    CHECK(startsWith(firstMatch(L"Segoe UI", 21, 400).family, g_displayAvailable ? L"SF Pro Display" : L"SF Pro Text"));
    CHECK_EQ(firstMatch(L"Segoe UI Semibold", 10.5f, 400).weight, 600);   // la graisse vient du nom
    CHECK_EQ(firstMatch(L"Segoe UI Variable", 10.5f, 700).weight, 700);   // une épaisseur demandée est gardée
    CHECK(firstMatch(L"Consolas", 10.5f, 400).family == L"Consolas");     // le reste n'est jamais touché
    typo->Release();
    f6->Release();
}

TEST_CASE(look_xaml_fallback_base_family) {
    // XAML passe aussi la famille de base au repli de polices (IDWriteFontFallback1::MapCharacters) : les caractères
    // latins doivent sortir en SF Pro, pas en Segoe UI Variable.
    loadSettings();
    if (!g_textAvailable) return;
    IDWriteFactory2* f2 = nullptr;
    REQUIRE(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), reinterpret_cast<IUnknown**>(&f2))));
    IDWriteFontFallback* system = nullptr;
    REQUIRE(SUCCEEDED(f2->GetSystemFontFallback(&system)));
    IDWriteFontFallback1* fallback = nullptr;
    REQUIRE(SUCCEEDED(system->QueryInterface(__uuidof(IDWriteFontFallback1), reinterpret_cast<void**>(&fallback))));
    MapCharacters1_Original = reinterpret_cast<MapCharacters1_t>((*reinterpret_cast<void***>(fallback))[4]);
    IDWriteFontCollection* collection = nullptr;   // sans collection, DirectWrite ignore la famille de base
    REQUIRE(SUCCEEDED(f2->GetSystemFontCollection(&collection, FALSE)));
    LookTextSource text(L"Caractères Grâce");
    auto mapped = [&](const wchar_t* base) {
        DWRITE_FONT_AXIS_VALUE axes[] = {{DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE, 10.5f}, {DWRITE_FONT_AXIS_TAG_WEIGHT, 400}};
        UINT32 length = 0;
        FLOAT scale = 0;
        IDWriteFontFace5* face = nullptr;
        std::wstring got;
        if (SUCCEEDED(MapCharacters1_Hook(fallback, &text, 0, text.length, collection, base, axes, 2, &length, &scale, &face)) && face) {
            got = win32Family(face);
            face->Release();
        }
        return got;
    };
    CHECK(startsWith(mapped(L"Segoe UI Variable Text"), L"SF Pro Text"));
    CHECK(mapped(L"Consolas") == L"Consolas");
    collection->Release();
    fallback->Release();
    system->Release();
    f2->Release();
}

TEST_CASE(look_font_aliases_remember_recent_fonts) {
    look::FontAliases a;
    wchar_t face[LF_FACESIZE] = {};
    const void* one = reinterpret_cast<void*>(1);
    a.remember(one, L"Segoe UI");
    REQUIRE(a.find(one, face));
    CHECK(std::wstring(face) == L"Segoe UI");
    CHECK(!a.find(reinterpret_cast<void*>(2), face));
    a.remember(one, L"MS Shell Dlg 2");   // poignée réutilisée par GDI : le nom le plus récent
    REQUIRE(a.find(one, face));
    CHECK(std::wstring(face) == L"MS Shell Dlg 2");
    for (int i = 0; i < look::FontAliases::kSize; ++i) a.remember(reinterpret_cast<void*>(std::uintptr_t(100 + i)), L"X");
    CHECK(!a.find(one, face));   // oubliée derrière les plus récentes
}

// Boîtes de dialogue (Exécuter, Ouvrir…) : user32 vérifie avec GetTextFaceAliasW que la police obtenue porte le nom
// de son modèle (« Segoe UI », « MS Shell Dlg 2 ») ; sinon il prend la police bitmap « System » (mesuré). Le mod répond
// le nom demandé pour les polices qu'il a remplacées.
TEST_CASE(look_dialog_font_check_sees_requested_name) {
    loadSettings();
    if (!g_textAvailable) return;   // SF Pro absente : rien n'est remplacé
    HMODULE gdi = LoadLibraryW(L"gdi32full.dll");
    REQUIRE(gdi != nullptr);
    CreateFontIndirectExW_Original = reinterpret_cast<CreateFontIndirectExW_t>(GetProcAddress(gdi, "CreateFontIndirectExW"));
    GetTextFaceAliasW_Original = reinterpret_cast<GetTextFaceAliasW_t>(GetProcAddress(LoadLibraryW(L"gdi32.dll"), "GetTextFaceAliasW"));
    REQUIRE(CreateFontIndirectExW_Original != nullptr);
    REQUIRE(GetTextFaceAliasW_Original != nullptr);
    auto aliasOf = [](const wchar_t* requested) {
        ENUMLOGFONTEXDVW e{};
        e.elfEnumLogfontEx.elfLogFont.lfHeight = -16;
        e.elfEnumLogfontEx.elfLogFont.lfCharSet = DEFAULT_CHARSET;
        wcsncpy_s(e.elfEnumLogfontEx.elfLogFont.lfFaceName, requested, _TRUNCATE);
        HFONT f = CreateFontIndirectExW_Hook(&e);
        HDC dc = CreateCompatibleDC(nullptr);
        HGDIOBJ old = SelectObject(dc, f);
        wchar_t got[LF_FACESIZE] = {}, real[LF_FACESIZE] = {};
        const int n = GetTextFaceAliasW_Hook(dc, LF_FACESIZE, got);
        GetTextFaceW(dc, LF_FACESIZE, real);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(f);
        return std::make_tuple(n, std::wstring(got), std::wstring(real));
    };
    const auto [n, alias, real] = aliasOf(L"Segoe UI");
    CHECK(alias == L"Segoe UI");
    CHECK_EQ(n, 9);                     // caractères copiés, zéro final compris (comme Windows)
    CHECK(real == L"SF Pro Text");      // la police dessinée reste bien SF Pro
    CHECK(std::get<1>(aliasOf(L"MS Shell Dlg 2")) == L"MS Shell Dlg 2");
    CHECK(std::get<1>(aliasOf(L"Consolas")) == L"Consolas");   // pas remplacée : la réponse de Windows
}
