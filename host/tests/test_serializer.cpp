// test_serializer.cpp — verifies byte-exact wire layout produced by TupleSerializer
#include "test_helpers.hpp"
#include "tuple_serializer.hpp"
#include <cstdio>
#include <vector>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void check_bytes(const char* name,
                         const std::vector<uint8_t>& got,
                         const std::vector<uint8_t>& want) {
    if (got == want) {
        std::printf("  OK  %s\n", name);
        return;
    }
    std::fprintf(stderr, "FAIL  %s:\n  got  [", name);
    for (size_t i = 0; i < got.size();  ++i) std::fprintf(stderr, "%02X%s", got[i],  i+1<got.size()  ?",":"");
    std::fprintf(stderr, "]\n  want [");
    for (size_t i = 0; i < want.size(); ++i) std::fprintf(stderr, "%02X%s", want[i], i+1<want.size() ?",":"");
    std::fprintf(stderr, "]\n");
    ++g_failures;
}

// ─── Tests ───────────────────────────────────────────────────────────────────

static void test_configure_int32() {
    TupleSerializer ser(KEY_INT32);
    JoinConfig cfg;
    cfg.algorithm = 0;
    cfg.key_type  = KEY_INT32;

    auto got = ser.serialize_configure(cfg, 3, 4, 0x7Au);

    // Expected layout (16 bytes):
    // [MSG_CONFIGURE=0x01][count=1 LE: 0x01,0x00]
    // [algorithm=0x00][key_type=0x01]
    // [rx_buf_slots=512 LE: 0x00,0x02]
    // [inner_count=3 LE: 0x03,0x00,0x00,0x00]
    // [outer_count=4 LE: 0x04,0x00,0x00,0x00]
    // [session_id=0x7A]
    std::vector<uint8_t> want = {
        0x01, 0x01, 0x00,           // header
        0x00, 0x01,                 // algorithm, key_type
        0x00, 0x02,                 // rx_buf_slots = 512 LE
        0x03, 0x00, 0x00, 0x00,    // inner_count = 3
        0x04, 0x00, 0x00, 0x00,    // outer_count = 4
        0x7A,                       // session_id
    };
    check_bytes("configure_int32", got, want);
}

static void test_inner_batch_int32() {
    TupleSerializer ser(KEY_INT32);

    InputTuple t;
    t.key      = 42;
    t.tid.blkno = 1;
    t.tid.offno = 3;

    auto got = ser.serialize_batch(MSG_INNER_DATA, &t, 1, 256);

    // Expected layout (13 bytes):
    // [MSG_INNER_DATA=0x02][count=1 LE: 0x01,0x00]
    // [key=42 LE i32: 0x2A,0x00,0x00,0x00]
    // [blkno=1 LE: 0x01,0x00,0x00,0x00]
    // [offno=3 LE: 0x03,0x00]
    std::vector<uint8_t> want = {
        0x02, 0x01, 0x00,
        0x2A, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x03, 0x00,
    };
    check_bytes("inner_batch_int32", got, want);
}

static void test_inner_batch_int64() {
    TupleSerializer ser(KEY_INT64);

    InputTuple t;
    t.key      = 42;
    t.tid.blkno = 1;
    t.tid.offno = 3;

    auto got = ser.serialize_batch(MSG_INNER_DATA, &t, 1, 256);

    // Expected layout (17 bytes):
    // [MSG_INNER_DATA=0x02][count=1 LE: 0x01,0x00]
    // [key=42 LE i64: 0x2A,0x00,0x00,0x00,0x00,0x00,0x00,0x00]
    // [blkno=1 LE: 0x01,0x00,0x00,0x00]
    // [offno=3 LE: 0x03,0x00]
    std::vector<uint8_t> want = {
        0x02, 0x01, 0x00,
        0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x01, 0x00, 0x00, 0x00,
        0x03, 0x00,
    };
    check_bytes("inner_batch_int64", got, want);
}

static void test_outer_batch_two_tuples() {
    TupleSerializer ser(KEY_INT32);

    InputTuple ts[2];
    ts[0].key = 10; ts[0].tid = {0, 1};
    ts[1].key = 20; ts[1].tid = {0, 2};

    auto got = ser.serialize_batch(MSG_OUTER_DATA, ts, 2, 256);

    // 3 + 2×10 = 23 bytes
    std::vector<uint8_t> want = {
        0x03, 0x02, 0x00,               // MSG_OUTER_DATA, count=2
        0x0A, 0x00, 0x00, 0x00,         // key=10
        0x00, 0x00, 0x00, 0x00,         // blkno=0
        0x01, 0x00,                     // offno=1
        0x14, 0x00, 0x00, 0x00,         // key=20
        0x00, 0x00, 0x00, 0x00,         // blkno=0
        0x02, 0x00,                     // offno=2
    };
    check_bytes("outer_batch_two_tuples", got, want);
}

static void test_batch_respects_max_count() {
    TupleSerializer ser(KEY_INT32);

    InputTuple ts[4] = {};
    for (int i = 0; i < 4; ++i) { ts[i].key = i + 1; ts[i].tid = {0, (uint16_t)(i+1)}; }

    // max_count = 2 → only first 2 tuples serialized
    auto got = ser.serialize_batch(MSG_INNER_DATA, ts, 4, 2);
    CHECK_EQ(got.size(), 3u + 2u * 10u);  // header + 2 int32 tuples
    CHECK_EQ(got[1], 0x02u);              // count field = 2
}

static void test_empty_batch_returns_empty() {
    TupleSerializer ser(KEY_INT32);
    auto got = ser.serialize_batch(MSG_INNER_DATA, nullptr, 0, 256);
    CHECK(got.empty());
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    test_configure_int32();
    test_inner_batch_int32();
    test_inner_batch_int64();
    test_outer_batch_two_tuples();
    test_batch_respects_max_count();
    test_empty_batch_returns_empty();
    return TEST_RESULT();
}
