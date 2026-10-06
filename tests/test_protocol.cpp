#include <cstring>

#include "minitest.h"
#include "../src/ipc/protocol.h"

TEST_CASE(protocol_roundtrip) {
    auto bytes = md::ipc::encode(md::ipc::makeProgress({0x1234, 2, 50, 100}));
    md::ipc::Decoder d;
    d.feed(bytes.data(), bytes.size());
    auto m = d.next();
    REQUIRE(m.has_value());
    auto p = md::ipc::parseProgress(*m);
    REQUIRE(p.has_value());
    CHECK_EQ(p->hwnd, 0x1234ull);
    CHECK_EQ(p->state, 2u);
    CHECK_EQ(p->completed, 50ull);
    CHECK_EQ(p->total, 100ull);
    CHECK(!d.next().has_value());
}

TEST_CASE(protocol_overlay_and_flash_roundtrip) {
    auto o = md::ipc::parseOverlay(md::ipc::makeOverlay({77, true}));
    REQUIRE(o.has_value());
    CHECK(o->hasOverlay);
    CHECK_EQ(o->hwnd, 77ull);
    auto f = md::ipc::parseFlash(md::ipc::makeFlash({9}));
    REQUIRE(f.has_value());
    CHECK_EQ(f->hwnd, 9ull);
    CHECK(!md::ipc::parseFlash(md::ipc::makeOverlay({1, false})).has_value());
}

TEST_CASE(protocol_partial_frames) {
    auto a = md::ipc::encode(md::ipc::makeFlash({7}));
    auto b = md::ipc::encode({md::ipc::MsgType::Heartbeat, {}});
    std::vector<uint8_t> all(a);
    all.insert(all.end(), b.begin(), b.end());
    md::ipc::Decoder d;
    int got = 0;
    for (auto byte : all) {
        d.feed(&byte, 1);
        while (d.next()) ++got;
    }
    CHECK_EQ(got, 2);
    CHECK(!d.failed());
}

TEST_CASE(protocol_rejects_bad_magic) {
    std::vector<uint8_t> junk(12, 0xAB);
    md::ipc::Decoder d;
    d.feed(junk.data(), junk.size());
    CHECK(!d.next().has_value());
    CHECK(d.failed());
}

TEST_CASE(protocol_rejects_oversized_payload) {
    auto f = md::ipc::encode({md::ipc::MsgType::Heartbeat, {}});
    uint32_t big = md::ipc::kMaxPayload + 1;
    std::memcpy(&f[8], &big, 4);
    md::ipc::Decoder d;
    d.feed(f.data(), f.size());
    CHECK(!d.next().has_value());
    CHECK(d.failed());
}

TEST_CASE(protocol_wrong_payload_size_is_rejected) {
    md::ipc::Message m{md::ipc::MsgType::Flash, {1, 2, 3}};
    CHECK(!md::ipc::parseFlash(m).has_value());
}

TEST_CASE(protocol_header_layout_is_stable) {
    // Le mod Windhawk duplique ce format : toute modification doit changer kVersion.
    auto f = md::ipc::encode({md::ipc::MsgType::Heartbeat, {}});
    REQUIRE(f.size() == 12);
    CHECK_EQ(int(f[0]), 0x4D);  // 'M'
    CHECK_EQ(int(f[3]), 0x4B);  // 'K'
    CHECK_EQ(int(f[4]), 1);     // version
    CHECK_EQ(int(f[6]), 1);     // type Heartbeat
}
