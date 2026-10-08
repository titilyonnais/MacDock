// ==WindhawkMod==
// @id              macdock-look
// @name            MacDock - macOS Look
// @description     The macOS system font (SF Pro) in every app, for MacDock
// @version         1.3.0
// @author          MacDock
// @include         *
// @exclude         MacDock.exe
// @exclude         MacMenuBar.exe
// @exclude         MacDockLauncher.exe
// @exclude         dwm.exe
// @exclude         csrss.exe
// @exclude         winlogon.exe
// @exclude         LogonUI.exe
// @exclude         lsass.exe
// @exclude         services.exe
// @exclude         fontdrvhost.exe
// @exclude         audiodg.exe
// @exclude         consent.exe
// @exclude         *\steamapps\*
// @exclude         *\Steam\steam.exe
// @exclude         *\Epic Games\*
// @exclude         *\Riot Games\*
// @exclude         *\Riot Vanguard\*
// @exclude         *\EasyAntiCheat*
// @exclude         *\BattlEye\*
// @exclude         *\EA Games\*
// @exclude         *\Electronic Arts\*
// @exclude         *\Ubisoft\*
// @exclude         *\Battle.net\*
// @exclude         *\GOG Galaxy\Games\*
// @exclude         *\XboxGames\*
// @exclude         *\Rockstar Games\*
// @exclude         *\Assetto Corsa*
// @exclude         *\Le Mans Ultimate\*
// @exclude         *\iRacing\*
// @exclude         *\RaceRoom*
// @exclude         *\rFactor*
// @exclude         *\Euro Truck Simulator*
// @exclude         *\Microsoft Flight Simulator*
// @exclude         *\MSFS*
// @exclude         *\Counter-Strike*
// @exclude         *\Battlefield*
// @exclude         *\Call of Duty*
// @exclude         cs2.exe
// @exclude         VALORANT*.exe
// @exclude         vgc.exe
// @exclude         *\Roblox\*
// @exclude         *\HoYoPlay\*
// @exclude         *\Battlestate Games\*
// @exclude         *\Overwatch\*
// @exclude         *\World of Warcraft\*
// @exclude         *\Diablo*
// @exclude         *\FACEIT\*
// @exclude         *\osu!\*
// @exclude         WINWORD.EXE
// @exclude         EXCEL.EXE
// @exclude         POWERPNT.EXE
// @exclude         ONENOTE.EXE
// @exclude         MSACCESS.EXE
// @exclude         OUTLOOK.EXE
// @exclude         soffice.bin
// @exclude         Acrobat.exe
// @exclude         AcroRd32.exe
// @architecture    x86-64
// @compilerOptions -lgdi32 -ldwrite -luser32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# MacDock - macOS Look

Companion mod of **MacDock** (a macOS-style Dock and menu bar for Windows).

Every app draws its interface with the macOS system font: the Windows UI fonts (Segoe UI, Segoe UI Variable, Tahoma,
MS Shell Dlg) are replaced by **SF Pro Text**, and by **SF Pro Display** for large titles, as on macOS.

- Works for classic Win32 apps and dialogs (GDI) and for modern apps (DirectWrite: File Explorer, Notepad, WinUI,
  Chromium and Electron apps). Dialogs that ask for Segoe UI or MS Shell Dlg 2 (Run, Open, Save As) keep SF Pro
  instead of falling back to the old System font.
- Icon fonts (Segoe Fluent Icons, Segoe MDL2 Assets, Segoe UI Emoji, Segoe UI Symbol) are never touched.
- Nothing changes if the macOS font is not installed.
- Games and anti-cheat software are excluded, and so are Office and PDF apps (documents keep their fonts).
- **Anti-cheat**: an exclusion here only keeps this mod out; Windhawk itself is still loaded. Add every online game you
  play to Windhawk's global list (Settings > Advanced > Process exclusion list): injecting code into a protected game
  can get an account banned.

SF Pro is Apple's font: install it yourself (it is not included).
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- textFont: SF Pro Text
  $name: Text font
  $description: Replaces Segoe UI and the other Windows interface fonts
- displayFont: SF Pro Display
  $name: Display font
  $description: Used for text of 20 points and more (titles), as on macOS
- gdi: true
  $name: Classic apps (GDI)
- directWrite: true
  $name: Modern apps (DirectWrite)
*/
// ==/WindhawkModSettings==

#include <windows.h>

#include <cwchar>

// ---- Logique pure (testée par MacDock : tests/test_windhawk_look.cpp) ----
namespace look {

enum class Role { None, Text, Display };

struct Mapping {
    Role role = Role::None;
    int weight = 0;   // graisse impliquée par le nom (« Segoe UI Semibold ») ; 0 = celle demandée
};

// Polices de l'interface de Windows à remplacer (comparaison sans casse) ; jamais les polices d'icônes.
inline Mapping mappingFor(const wchar_t* face) {
    if (!face || !*face) return {};
    struct Entry {
        const wchar_t* name;
        Role role;
        int weight;
    };
    static const Entry kEntries[] = {
        {L"Segoe UI", Role::Text, 0},
        {L"Segoe UI Variable", Role::Text, 0},
        {L"Segoe UI Variable Text", Role::Text, 0},
        {L"Segoe UI Variable Small", Role::Text, 0},
        {L"Segoe UI Variable Display", Role::Display, 0},
        {L"Segoe UI Semibold", Role::Text, 600},
        {L"Segoe UI Semilight", Role::Text, 350},
        {L"Segoe UI Light", Role::Text, 300},
        {L"Segoe UI Black", Role::Text, 900},
        {L"Segoe UI Variable Text Semibold", Role::Text, 600},
        {L"Segoe UI Variable Display Semibold", Role::Display, 600},
        // Pas Tahoma ni Microsoft Sans Serif : elles servent aussi dans les documents.
        {L"MS Shell Dlg", Role::Text, 0},
        {L"MS Shell Dlg 2", Role::Text, 0},
    };
    for (const Entry& e : kEntries)
        if (_wcsicmp(face, e.name) == 0) return {e.role, e.weight};
    return {};
}

// Points d'une hauteur GDI (pixels du contexte DPI de l'appelant) : négative, hauteur des caractères ; positive,
// hauteur de cellule (un cinquième de plus environ).
inline double pointsForHeight(long height, unsigned dpi) {
    if (!height || !dpi) return 0;
    const double px = height < 0 ? -double(height) : height * 0.8;
    return px * 72.0 / dpi;
}

// Rôle d'après la taille du texte (points ; 0 = inconnue) : Display à partir de 20 pt, comme macOS.
inline Role roleForSize(Role role, double points) {
    if (role == Role::Text && points >= 20) return Role::Display;
    return role;
}

// Polices GDI remplacées, avec le nom que l'app avait demandé. Tampon tournant : GDI réutilise les poignées, et seule
// compte la police tout juste créée (user32 vérifie le nom d'une police de boîte de dialogue aussitôt après l'avoir
// créée). Sans verrou : l'appelant le tient.
class FontAliases {
public:
    static constexpr int kSize = 256;
    void remember(const void* font, const wchar_t* face) {
        Entry& e = entries_[next_];
        e.font = font;
        wcsncpy_s(e.face, face ? face : L"", _TRUNCATE);
        next_ = (next_ + 1) % kSize;
    }
    // Nom demandé à la création de cette police (la plus récente sous cette poignée), copié dans `out`.
    bool find(const void* font, wchar_t (&out)[LF_FACESIZE]) const {
        for (int i = 1; i <= kSize; ++i) {
            const Entry& e = entries_[(next_ - i + kSize) % kSize];
            if (e.font && e.font == font) {
                wcsncpy_s(out, e.face, _TRUNCATE);
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        const void* font = nullptr;
        wchar_t face[LF_FACESIZE] = {};
    };
    Entry entries_[kSize];
    int next_ = 0;
};

}  // namespace look

#ifndef MACDOCK_LOOK_TEST

#include <dwrite_3.h>

namespace {

wchar_t g_textFont[LF_FACESIZE] = L"SF Pro Text";
wchar_t g_displayFont[LF_FACESIZE] = L"SF Pro Display";
bool g_gdi = true, g_directWrite = true;
bool g_textAvailable = false, g_displayAvailable = false;

const wchar_t* replacementFor(look::Role role) {
    if (role == look::Role::Display) return g_displayAvailable ? g_displayFont : (g_textAvailable ? g_textFont : nullptr);
    if (role == look::Role::Text) return g_textAvailable ? g_textFont : nullptr;
    return nullptr;
}

// ---- GDI ----
using CreateFontIndirectExW_t = HFONT(WINAPI*)(const ENUMLOGFONTEXDVW*);
CreateFontIndirectExW_t CreateFontIndirectExW_Original;
look::FontAliases g_aliases;
SRWLOCK g_aliasLock = SRWLOCK_INIT;

HFONT WINAPI CreateFontIndirectExW_Hook(const ENUMLOGFONTEXDVW* font) {
    if (g_gdi && font) {
        const LOGFONTW& lf = font->elfEnumLogfontEx.elfLogFont;
        const look::Mapping m = look::mappingFor(lf.lfFaceName);
        // Hauteur en pixels du contexte DPI du fil appelant (192 à 200 % dans une app qui gère le DPI).
        UINT dpi = GetDpiFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
        if (!dpi) dpi = GetDpiForSystem();
        const double points = look::pointsForHeight(lf.lfHeight, dpi);
        if (const wchar_t* to = replacementFor(look::roleForSize(m.role, points))) {
            ENUMLOGFONTEXDVW copy = *font;
            LOGFONTW& out = copy.elfEnumLogfontEx.elfLogFont;
            wcsncpy_s(out.lfFaceName, to, _TRUNCATE);
            if (m.weight && (out.lfWeight == FW_DONTCARE || out.lfWeight == FW_NORMAL)) out.lfWeight = m.weight;
            const HFONT replaced = CreateFontIndirectExW_Original(&copy);
            if (replaced) {   // pour la vérification des boîtes de dialogue (GetTextFaceAliasW)
                AcquireSRWLockExclusive(&g_aliasLock);
                g_aliases.remember(replaced, lf.lfFaceName);
                ReleaseSRWLockExclusive(&g_aliasLock);
            }
            return replaced;
        }
    }
    return CreateFontIndirectExW_Original(font);
}

// user32 vérifie que la police d'une boîte de dialogue porte le nom de son modèle (« Segoe UI », « MS Shell Dlg 2 ») ;
// sinon il prend la police bitmap « System » (Exécuter, Ouvrir, Enregistrer sous…). Pour les polices remplacées : le
// nom demandé, la police dessinée reste SF Pro.
using GetTextFaceAliasW_t = int(WINAPI*)(HDC, int, LPWSTR);
GetTextFaceAliasW_t GetTextFaceAliasW_Original;

int WINAPI GetTextFaceAliasW_Hook(HDC dc, int count, LPWSTR out) {
    const int r = GetTextFaceAliasW_Original(dc, count, out);
    if (r <= 0 || !g_gdi) return r;
    wchar_t face[LF_FACESIZE] = {};
    AcquireSRWLockShared(&g_aliasLock);
    const bool found = g_aliases.find(GetCurrentObject(dc, OBJ_FONT), face);
    ReleaseSRWLockShared(&g_aliasLock);
    if (!found || !face[0]) return r;
    if (!out) return int(wcslen(face)) + 1;   // taille demandée
    if (count <= 0) return r;
    wcsncpy_s(out, size_t(count), face, _TRUNCATE);
    return int(wcslen(out)) + 1;   // caractères copiés, zéro final compris (comme Windows)
}

int CALLBACK fontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM found) {
    *reinterpret_cast<bool*>(found) = true;
    return 0;
}

bool gdiHasFont(const wchar_t* face) {
    HDC dc = GetDC(nullptr);
    if (!dc) return false;
    LOGFONTW lf{};
    lf.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    bool found = false;
    EnumFontFamiliesExW(dc, &lf, fontExists, reinterpret_cast<LPARAM>(&found), 0);
    ReleaseDC(nullptr, dc);
    return found;
}

// ---- DirectWrite ----
using FindFamilyName_t = HRESULT(STDMETHODCALLTYPE*)(IDWriteFontCollection*, const WCHAR*, UINT32*, BOOL*);
FindFamilyName_t FindFamilyName_Original;

HRESULT STDMETHODCALLTYPE FindFamilyName_Hook(IDWriteFontCollection* self, const WCHAR* name, UINT32* index, BOOL* exists) {
    if (g_directWrite)
        if (const wchar_t* to = replacementFor(look::mappingFor(name).role)) {
            const HRESULT hr = FindFamilyName_Original(self, to, index, exists);
            if (SUCCEEDED(hr) && exists && *exists) return hr;   // sinon : la police d'origine
        }
    return FindFamilyName_Original(self, name, index, exists);
}

using CreateTextFormat_t = HRESULT(STDMETHODCALLTYPE*)(IDWriteFactory*, const WCHAR*, IDWriteFontCollection*,
                                                       DWRITE_FONT_WEIGHT, DWRITE_FONT_STYLE, DWRITE_FONT_STRETCH, FLOAT,
                                                       const WCHAR*, IDWriteTextFormat**);
CreateTextFormat_t CreateTextFormat_Original;

// Polices variables (Segoe UI Variable) demandées avec des axes : SF Pro n'est pas variable, l'épaisseur y choisit la
// graisse et la taille optique est sans effet. Copie des axes (8 au plus) où la graisse du nom (« Segoe UI Semibold »)
// remplace une épaisseur normale ou absente.
UINT32 axesFor(const look::Mapping& m, const DWRITE_FONT_AXIS_VALUE* axes, UINT32 count, DWRITE_FONT_AXIS_VALUE (&out)[8]) {
    UINT32 n = 0;
    bool weight = false;
    for (UINT32 i = 0; axes && i < count && n < 7; ++i) {
        out[n] = axes[i];
        if (out[n].axisTag == DWRITE_FONT_AXIS_TAG_WEIGHT) {
            weight = true;
            if (m.weight && out[n].value == float(DWRITE_FONT_WEIGHT_NORMAL)) out[n].value = float(m.weight);
        }
        ++n;
    }
    if (!weight && m.weight) out[n++] = {DWRITE_FONT_AXIS_TAG_WEIGHT, float(m.weight)};
    return n;
}

// Taille optique demandée (points), 0 sans cet axe : elle choisit Display pour les grands textes.
double opticalSize(const DWRITE_FONT_AXIS_VALUE* axes, UINT32 count) {
    for (UINT32 i = 0; axes && i < count; ++i)
        if (axes[i].axisTag == DWRITE_FONT_AXIS_TAG_OPTICAL_SIZE) return axes[i].value;
    return 0;
}

// IDWriteFactory6::CreateTextFormat, avec axes (police variable).
using CreateTextFormat6_t = HRESULT(STDMETHODCALLTYPE*)(IDWriteFactory6*, const WCHAR*, IDWriteFontCollection*,
                                                        const DWRITE_FONT_AXIS_VALUE*, UINT32, FLOAT, const WCHAR*,
                                                        IDWriteTextFormat3**);
CreateTextFormat6_t CreateTextFormat6_Original;

HRESULT STDMETHODCALLTYPE CreateTextFormat6_Hook(IDWriteFactory6* self, const WCHAR* family, IDWriteFontCollection* collection,
                                                 const DWRITE_FONT_AXIS_VALUE* axes, UINT32 count, FLOAT size,
                                                 const WCHAR* locale, IDWriteTextFormat3** out) {
    if (g_directWrite) {
        const look::Mapping m = look::mappingFor(family);
        if (const wchar_t* to = replacementFor(look::roleForSize(m.role, size * 72.0 / 96.0))) {
            DWRITE_FONT_AXIS_VALUE local[8];
            const UINT32 n = axesFor(m, axes, count, local);
            return CreateTextFormat6_Original(self, to, collection, n ? local : nullptr, n, size, locale, out);
        }
    }
    return CreateTextFormat6_Original(self, family, collection, axes, count, size, locale, out);
}

// IDWriteFontCollection2::GetMatchingFonts : le chemin par lequel XAML (Bloc-notes, Explorateur, Paramètres) choisit la
// police affichée, « Segoe UI Variable » et ses axes. La police d'origine si SF Pro n'est pas dans la collection.
using GetMatchingFonts_t = HRESULT(STDMETHODCALLTYPE*)(IDWriteFontCollection2*, const WCHAR*, const DWRITE_FONT_AXIS_VALUE*,
                                                       UINT32, IDWriteFontList2**);
GetMatchingFonts_t GetMatchingFonts_Original;

HRESULT STDMETHODCALLTYPE GetMatchingFonts_Hook(IDWriteFontCollection2* self, const WCHAR* family,
                                                const DWRITE_FONT_AXIS_VALUE* axes, UINT32 count, IDWriteFontList2** out) {
    if (g_directWrite && out) {
        const look::Mapping m = look::mappingFor(family);
        if (const wchar_t* to = replacementFor(look::roleForSize(m.role, opticalSize(axes, count)))) {
            DWRITE_FONT_AXIS_VALUE local[8];
            const UINT32 n = axesFor(m, axes, count, local);
            IDWriteFontList2* list = nullptr;
            if (SUCCEEDED(GetMatchingFonts_Original(self, to, n ? local : nullptr, n, &list)) && list) {
                if (list->GetFontCount() > 0) {
                    *out = list;
                    return S_OK;
                }
                list->Release();
            }
        }
    }
    return GetMatchingFonts_Original(self, family, axes, count, out);
}

// IDWriteFontFallback1::MapCharacters : XAML y passe la famille de base de chaque passage de texte ; le repli vers une
// autre police ne sert qu'aux caractères absents de SF Pro. La famille d'origine si SF Pro n'est pas dans la collection
// (sans collection, DirectWrite ignore de toute façon la famille de base).
using MapCharacters1_t = HRESULT(STDMETHODCALLTYPE*)(IDWriteFontFallback1*, IDWriteTextAnalysisSource*, UINT32, UINT32,
                                                     IDWriteFontCollection*, const WCHAR*, const DWRITE_FONT_AXIS_VALUE*,
                                                     UINT32, UINT32*, FLOAT*, IDWriteFontFace5**);
MapCharacters1_t MapCharacters1_Original;

HRESULT STDMETHODCALLTYPE MapCharacters1_Hook(IDWriteFontFallback1* self, IDWriteTextAnalysisSource* source, UINT32 position,
                                              UINT32 length, IDWriteFontCollection* collection, const WCHAR* family,
                                              const DWRITE_FONT_AXIS_VALUE* axes, UINT32 count, UINT32* mappedLength,
                                              FLOAT* scale, IDWriteFontFace5** face) {
    if (g_directWrite) {
        const look::Mapping m = look::mappingFor(family);
        if (const wchar_t* to = replacementFor(look::roleForSize(m.role, opticalSize(axes, count)))) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (collection) collection->FindFamilyName(to, &index, &exists);
            if (exists) {
                DWRITE_FONT_AXIS_VALUE local[8];
                const UINT32 n = axesFor(m, axes, count, local);
                return MapCharacters1_Original(self, source, position, length, collection, to, n ? local : nullptr, n,
                                               mappedLength, scale, face);
            }
        }
    }
    return MapCharacters1_Original(self, source, position, length, collection, family, axes, count, mappedLength, scale, face);
}

HRESULT STDMETHODCALLTYPE CreateTextFormat_Hook(IDWriteFactory* self, const WCHAR* family, IDWriteFontCollection* collection,
                                                DWRITE_FONT_WEIGHT weight, DWRITE_FONT_STYLE style,
                                                DWRITE_FONT_STRETCH stretch, FLOAT size, const WCHAR* locale,
                                                IDWriteTextFormat** out) {
    if (g_directWrite) {
        const look::Mapping m = look::mappingFor(family);
        // Taille en DIP (1/96 de pouce) : 20 pt = 26,7 DIP.
        if (const wchar_t* to = replacementFor(look::roleForSize(m.role, size * 72.0 / 96.0))) {
            if (m.weight && weight == DWRITE_FONT_WEIGHT_NORMAL) weight = DWRITE_FONT_WEIGHT(m.weight);
            return CreateTextFormat_Original(self, to, collection, weight, style, stretch, size, locale, out);
        }
    }
    return CreateTextFormat_Original(self, family, collection, weight, style, stretch, size, locale, out);
}

void loadSettings() {
    PCWSTR text = Wh_GetStringSetting(L"textFont");
    PCWSTR display = Wh_GetStringSetting(L"displayFont");
    if (text && *text) wcsncpy_s(g_textFont, text, _TRUNCATE);
    if (display && *display) wcsncpy_s(g_displayFont, display, _TRUNCATE);
    Wh_FreeStringSetting(text);
    Wh_FreeStringSetting(display);
    g_gdi = Wh_GetIntSetting(L"gdi") != 0;
    g_directWrite = Wh_GetIntSetting(L"directWrite") != 0;
    g_textAvailable = gdiHasFont(g_textFont);
    g_displayAvailable = gdiHasFont(g_displayFont);
}

}  // namespace

BOOL Wh_ModInit() {
    loadSettings();
    if (!g_textAvailable) {
        Wh_Log(L"%s n'est pas installée : rien n'est remplacé", g_textFont);
        return TRUE;
    }
    // GDI : l'implémentation (gdi32full), par où passent CreateFontIndirectW, CreateFontW et les polices du système.
    HMODULE gdi = LoadLibraryExW(L"gdi32full.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!gdi) gdi = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (void* target = gdi ? reinterpret_cast<void*>(GetProcAddress(gdi, "CreateFontIndirectExW")) : nullptr)
        Wh_SetFunctionHook(target, reinterpret_cast<void*>(CreateFontIndirectExW_Hook),
                           reinterpret_cast<void**>(&CreateFontIndirectExW_Original));
    // Vérification des boîtes de dialogue : user32 l'importe de gdi32.dll.
    HMODULE gdi32 = LoadLibraryExW(L"gdi32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (void* target = gdi32 ? reinterpret_cast<void*>(GetProcAddress(gdi32, "GetTextFaceAliasW")) : nullptr)
        Wh_SetFunctionHook(target, reinterpret_cast<void*>(GetTextFaceAliasW_Hook),
                           reinterpret_cast<void**>(&GetTextFaceAliasW_Original));
    // DirectWrite : les méthodes de la fabrique partagée et de la collection système (une seule implémentation pour
    // toutes les instances du processus).
    using DWriteCreateFactory_t = HRESULT(WINAPI*)(DWRITE_FACTORY_TYPE, REFIID, IUnknown**);
    HMODULE dwrite = LoadLibraryExW(L"dwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);   // jamais celle du dossier de l'app
    auto create = dwrite ? reinterpret_cast<DWriteCreateFactory_t>(GetProcAddress(dwrite, "DWriteCreateFactory")) : nullptr;
    IDWriteFactory* factory = nullptr;
    if (create && SUCCEEDED(create(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(&factory))) && factory) {
        void** factoryVtable = *reinterpret_cast<void***>(factory);
        Wh_SetFunctionHook(factoryVtable[15], reinterpret_cast<void*>(CreateTextFormat_Hook),
                           reinterpret_cast<void**>(&CreateTextFormat_Original));   // IDWriteFactory::CreateTextFormat
        IDWriteFactory6* factory6 = nullptr;   // Windows 10 1903 et plus : le chemin de XAML
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDWriteFactory6), reinterpret_cast<void**>(&factory6))) && factory6) {
            void** vtable6 = *reinterpret_cast<void***>(factory6);
            Wh_SetFunctionHook(vtable6[54], reinterpret_cast<void*>(CreateTextFormat6_Hook),
                               reinterpret_cast<void**>(&CreateTextFormat6_Original));   // IDWriteFactory6::CreateTextFormat
            factory6->Release();
        }
        IDWriteFontCollection* system = nullptr;
        if (SUCCEEDED(factory->GetSystemFontCollection(&system, FALSE)) && system) {
            void** collectionVtable = *reinterpret_cast<void***>(system);
            Wh_SetFunctionHook(collectionVtable[5], reinterpret_cast<void*>(FindFamilyName_Hook),
                               reinterpret_cast<void**>(&FindFamilyName_Original));   // IDWriteFontCollection::FindFamilyName
            IDWriteFontCollection2* system2 = nullptr;
            if (SUCCEEDED(system->QueryInterface(__uuidof(IDWriteFontCollection2), reinterpret_cast<void**>(&system2))) && system2) {
                void** vtable2 = *reinterpret_cast<void***>(system2);
                Wh_SetFunctionHook(vtable2[10], reinterpret_cast<void*>(GetMatchingFonts_Hook),
                                   reinterpret_cast<void**>(&GetMatchingFonts_Original));   // IDWriteFontCollection2::GetMatchingFonts
                system2->Release();
            }
            system->Release();
        }
        IDWriteFactory2* factory2 = nullptr;
        IDWriteFontFallback* fallback = nullptr;
        IDWriteFontFallback1* fallback1 = nullptr;
        if (SUCCEEDED(factory->QueryInterface(__uuidof(IDWriteFactory2), reinterpret_cast<void**>(&factory2))) && factory2 &&
            SUCCEEDED(factory2->GetSystemFontFallback(&fallback)) && fallback &&
            SUCCEEDED(fallback->QueryInterface(__uuidof(IDWriteFontFallback1), reinterpret_cast<void**>(&fallback1))) && fallback1) {
            void** fallbackVtable = *reinterpret_cast<void***>(fallback1);
            Wh_SetFunctionHook(fallbackVtable[4], reinterpret_cast<void*>(MapCharacters1_Hook),
                               reinterpret_cast<void**>(&MapCharacters1_Original));   // IDWriteFontFallback1::MapCharacters
        }
        if (fallback1) fallback1->Release();
        if (fallback) fallback->Release();
        if (factory2) factory2->Release();
        factory->Release();
    }
    return TRUE;
}

void Wh_ModSettingsChanged() {
    loadSettings();   // polices et interrupteurs relus ; les crochets restent posés
}

#endif  // MACDOCK_LOOK_TEST
