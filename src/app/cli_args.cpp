#include "cli_args.h"

#include <string_view>

namespace md {

std::wstring diagnosticMissingValue(const std::vector<std::wstring>& argv) {
    static constexpr std::wstring_view kWithValue[] = {L"--snapshot",       L"--apps-snapshot", L"--genie-snapshot",
                                                       L"--theme-snapshot", L"--capture-test",  L"--theme",
                                                       L"--spotlight-snapshot", L"--mission-snapshot"};
    for (std::size_t i = 1; i < argv.size(); ++i) {
        const std::wstring& a = argv[i];
        for (std::wstring_view opt : kWithValue) {
            if (a.size() > opt.size() && a.starts_with(opt) && a[opt.size()] == L'=') return a;   // --option=valeur
            if (a != opt) continue;
            if (i + 1 >= argv.size() || argv[i + 1].starts_with(L"--")) return a;
        }
    }
    return {};
}

} // namespace md
