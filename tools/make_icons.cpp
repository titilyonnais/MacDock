// make_icons <dossier> : écrit settings.ico, dock.ico et menubar.ico (plan 49), dessinés par le code. Les fichiers sont
// versionnés dans res\ ; build.ps1 les lie aux exécutables (res\*.rc). À relancer si le dessin des icônes change :
//   powershell -File build.ps1 -Target icons ; build\Debug\make_icons.exe res
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/calib/png_io.h"
#include "../src/settings/app_icon.h"

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) {
        std::fwprintf(stderr, L"usage : make_icons <dossier>\n");
        return 2;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    const std::wstring dir = argv[1];
    const struct {
        md::AppIconKind kind;
        const wchar_t* name;
    } icons[] = {{md::AppIconKind::Settings, L"settings.ico"}, {md::AppIconKind::Dock, L"dock.ico"},
                 {md::AppIconKind::MenuBar, L"menubar.ico"}};
    int code = 0;
    for (const auto& icon : icons) {
        std::vector<std::pair<int, std::vector<std::uint8_t>>> pngs;
        for (int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
            const md::BgraImage im = md::renderAppIcon(icon.kind, size);
            pngs.push_back({size, md::encodePng(im.px.data(), UINT(im.w), UINT(im.h))});
            if (pngs.back().second.empty()) code = 1;
        }
        const std::vector<std::uint8_t> ico = md::icoFile(pngs);
        const std::wstring path = dir + L"\\" + icon.name;
        FILE* f = nullptr;
        if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
            std::fwprintf(stderr, L"impossible d'écrire %ls\n", path.c_str());
            code = 1;
            continue;
        }
        std::fwrite(ico.data(), 1, ico.size(), f);
        std::fclose(f);
        std::wprintf(L"%ls : %zu octets\n", path.c_str(), ico.size());
    }
    CoUninitialize();
    return code;
}
