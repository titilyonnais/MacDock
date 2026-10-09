// Mini-framework de tests sans dépendance externe.
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace minitest {

struct Case { const char* name; void (*fn)(); };

inline std::vector<Case>& registry() { static std::vector<Case> r; return r; }
inline int& failures() { static int f = 0; return f; }

struct Registrar { Registrar(const char* n, void (*f)()) { registry().push_back({n, f}); } };
struct RequireFailed {};
struct Skipped {};

// Machine d'intégration continue (GitHub Actions définit CI=true) : un test qui dépend de ce PC (sortie audio, app du
// Microsoft Store) y est sauté s'il manque ce qu'il lui faut ; ailleurs, il échoue comme d'habitude.
inline bool onCi() {
    char* v = nullptr;
    std::size_t n = 0;
    const bool set = _dupenv_s(&v, &n, "CI") == 0 && v && *v;
    std::free(v);
    return set;
}

template <class T>
std::string show(const T& v) {
    if constexpr (requires(std::ostream& o, const T& x) { o << x; }) {
        std::ostringstream o; o << v; return o.str();
    } else {
        return "<?>";
    }
}

inline void fail(const char* file, int line, const std::string& msg) {
    ++failures();
    std::printf("    %s(%d): %s\n", file, line, msg.c_str());
}

inline int runAll(const char* filter) {
    int failedCases = 0, ran = 0, skipped = 0;
    for (auto& c : registry()) {
        if (filter && !std::string(c.name).contains(filter)) continue;
        ++ran;
        int before = failures();
        bool skip = false;
        try { c.fn(); } catch (RequireFailed&) {
        } catch (Skipped&) { skip = true;
        } catch (std::exception& e) { fail(__FILE__, __LINE__, std::string("exception: ") + e.what()); }
        catch (...) { fail(__FILE__, __LINE__, "exception inconnue"); }
        bool ok = failures() == before;
        if (!ok) ++failedCases;
        if (skip && ok) ++skipped;
        std::printf("[%s] %s\n", !ok ? "FAIL" : skip ? "SKIP" : "PASS", c.name);
    }
    if (skipped) std::printf("\n%d cas, %d en echec, %d sautes (CI)\n", ran, failedCases, skipped);
    else std::printf("\n%d cas, %d en echec\n", ran, failedCases);
    return failedCases;
}

} // namespace minitest

#define MT_CAT2(a, b) a##b
#define MT_CAT(a, b) MT_CAT2(a, b)
#define TEST_CASE(name)                                                        \
    static void name();                                                        \
    static minitest::Registrar MT_CAT(mt_reg_, name)(#name, &name);            \
    static void name()

#define CHECK(expr)                                                            \
    do { if (!(expr)) minitest::fail(__FILE__, __LINE__, "CHECK(" #expr ")"); } while (0)

// Sur la CI seulement : saute le test si `cond` (ce qu'il lui faut manque sur la machine).
#define SKIP_ON_CI_IF(cond, reason)                                            \
    do { if (minitest::onCi() && (cond)) { std::printf("    saute : %s\n", reason); \
                                            throw minitest::Skipped{}; } } while (0)

#define REQUIRE(expr)                                                          \
    do { if (!(expr)) { minitest::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")"); \
                        throw minitest::RequireFailed{}; } } while (0)

#define CHECK_EQ(a, b)                                                         \
    do { auto&& mt_a = (a); auto&& mt_b = (b);                                 \
         if (!(mt_a == mt_b)) minitest::fail(__FILE__, __LINE__,               \
             "CHECK_EQ(" #a ", " #b ") : " + minitest::show(mt_a) + " != " + minitest::show(mt_b)); } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
    do { double mt_a = double(a), mt_b = double(b);                            \
         if (!(std::fabs(mt_a - mt_b) <= double(eps))) minitest::fail(__FILE__, __LINE__, \
             "CHECK_NEAR(" #a ", " #b ") : " + std::to_string(mt_a) + " vs " + std::to_string(mt_b)); } while (0)
