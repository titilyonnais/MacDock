#include "minitest.h"
#include "../src/core/strings.h"

TEST_CASE(strings_utf8_roundtrip) {
    std::wstring w = L"Téléchargements – 日本";
    CHECK(md::fromUtf8(md::toUtf8(w)) == w);
    CHECK_EQ(md::toUtf8(L"é"), std::string("\xC3\xA9"));
}

TEST_CASE(strings_lower) {
    CHECK(md::toLower(L"C:\\Program Files\\APP.EXE") == L"c:\\program files\\app.exe");
}

TEST_CASE(strings_invalid_utf8_does_not_throw) {
    CHECK(md::fromUtf8("\xFF\xFE").size() > 0);
}
