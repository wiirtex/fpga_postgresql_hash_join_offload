#include "hash_join_kernel.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>
#include <cstdint>

// ─── Simulation DDR2 buffer ───────────────────────────────────────────────────
// Emulates the DDR2 SDRAM accessible via AXI Master in hardware.
// GRACE_OUTER_AREA_BASE (6291456 words, 48 MB) + outer area (~same) ≈ 100 MB.
// Declared global (BSS) to avoid stack overflow.
static int64_t g_sim_ddr2[GRACE_DDR2_TOTAL_WORDS];

// ─── Stream helpers ───────────────────────────────────────────────────────────

static void push_byte(ByteStream& s, uint8_t v, bool last = false) {
    AxisByte b;
    b.data = v;
    b.last = last ? 1u : 0u;
    b.keep = 0xFFu;
    s.write(b);
}

static void push_u16(ByteStream& s, uint16_t v, bool last = false) {
    push_byte(s,  v & 0xFFu);
    push_byte(s, (v >> 8u) & 0xFFu, last);
}

static void push_u32(ByteStream& s, uint32_t v, bool last = false) {
    push_byte(s, (v >>  0u) & 0xFFu);
    push_byte(s, (v >>  8u) & 0xFFu);
    push_byte(s, (v >> 16u) & 0xFFu);
    push_byte(s, (v >> 24u) & 0xFFu, last);
}

static uint8_t  pop_byte(ByteStream& s) { return (uint8_t)s.read().data; }
static uint16_t pop_u16(ByteStream& s)  {
    uint16_t b0 = pop_byte(s);
    uint16_t b1 = pop_byte(s);
    return b0 | (uint16_t)(b1 << 8u);
}
static uint32_t pop_u32(ByteStream& s)  {
    uint32_t b0 = pop_byte(s);
    uint32_t b1 = pop_byte(s);
    uint32_t b2 = pop_byte(s);
    uint32_t b3 = pop_byte(s);
    return b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
}
static uint64_t pop_u64(ByteStream& s)  {
    const uint64_t lo = pop_u32(s);
    const uint64_t hi = pop_u32(s);
    return lo | (hi << 32u);
}

// ─── Frame builders ───────────────────────────────────────────────────────────

// Send CONFIGURE frame (13-byte payload)
static void send_configure(ByteStream& rx,
                           uint8_t algorithm, uint8_t key_type,
                           uint16_t rx_buf, uint32_t inner_count,
                           uint32_t outer_count, uint8_t session_id = 1u) {
    push_byte(rx, MSG_CONFIGURE);
    push_u16(rx, 1u);              // count = 1 ConfigurePayload
    push_byte(rx, algorithm);
    push_byte(rx, key_type);
    push_u16(rx, rx_buf);
    push_u32(rx, inner_count);
    push_u32(rx, outer_count);
    push_byte(rx, session_id, /*last=*/true);
}

// Send INNER_DATA batch (int32 keys)
static void send_inner_data(ByteStream& rx, const FpgaTuple32* tuples, uint16_t count) {
    push_byte(rx, MSG_INNER_DATA);
    push_u16(rx, count);
    for (uint16_t i = 0u; i < count; i++) {
        push_u32(rx, (uint32_t)tuples[i].key);
        push_u32(rx, tuples[i].tid.blkno);
        push_u16(rx, tuples[i].tid.offno, /*last=*/(i == (uint16_t)(count - 1u)));
    }
}

// Send OUTER_DATA batch (int32 keys)
static void send_outer_data(ByteStream& rx, const FpgaTuple32* tuples, uint16_t count) {
    push_byte(rx, MSG_OUTER_DATA);
    push_u16(rx, count, /*last=*/(count == 0u));
    for (uint16_t i = 0u; i < count; i++) {
        push_u32(rx, (uint32_t)tuples[i].key);
        push_u32(rx, tuples[i].tid.blkno);
        push_u16(rx, tuples[i].tid.offno, /*last=*/(i == (uint16_t)(count - 1u)));
    }
}

// ─── Frame drain helpers ──────────────────────────────────────────────────────

// Drain and count result pairs from the TX stream until MSG_STATUS(DONE)
struct TxDrainResult {
    uint32_t ack_count;     // number of ACK frames received
    uint32_t result_count;  // total ResultPairs received
    uint32_t timing_count;  // number of TIMING frames received
    bool     got_done;      // saw MSG_STATUS with PHASE_DONE
    std::vector<ResultPair> results;
};

static TxDrainResult drain_tx(ByteStream& tx) {
    TxDrainResult r = {0u, 0u, 0u, false, {}};
    // Drain up to 10000 frames to avoid infinite loops in simulation
    for (int f = 0; f < 10000; f++) {
        if (tx.empty()) break;
        const uint8_t  msg_type = (uint8_t)tx.read().data;
        const uint16_t count    = pop_u16(tx);

        if (msg_type == MSG_ACK) {
            // Read StatusPayload (12 bytes): phase, session_id, credit, processed, matched
            const uint8_t phase = pop_byte(tx);
            pop_byte(tx);            // session_id
            pop_u16(tx);             // credit
            pop_u32(tx);             // rows_processed
            pop_u32(tx);             // rows_matched
            r.ack_count++;
            (void)phase;
        } else if (msg_type == MSG_RESULT) {
            // Drain 'count' ResultPairs (12 bytes each)
            for (uint16_t i = 0u; i < count; i++) {
                ResultPair rp;
                rp.inner_tid.blkno = pop_u32(tx);
                rp.inner_tid.offno = pop_u16(tx);
                rp.outer_tid.blkno = pop_u32(tx);
                rp.outer_tid.offno = pop_u16(tx);
                r.results.push_back(rp);
                r.result_count++;
            }
        } else if (msg_type == MSG_STATUS) {
            const uint8_t phase = pop_byte(tx);
            pop_byte(tx);
            pop_u16(tx);
            pop_u32(tx);
            pop_u32(tx);
            if (phase == PHASE_DONE) r.got_done = true;
        } else if (msg_type == MSG_TIMING) {
            assert(count == TIMING_SUMMARY_COUNT);
            const uint16_t version = pop_u16(tx);
            pop_u16(tx);  // flags
            pop_u32(tx);  // clock_hz
            pop_u32(tx);  // inner_rows
            pop_u32(tx);  // outer_rows
            pop_u32(tx);  // matched_rows
            pop_u32(tx);  // inner_frames
            pop_u32(tx);  // outer_frames
            pop_u32(tx);  // result_frames
            pop_u32(tx);  // ack_frames
            pop_u32(tx);  // debug_frames
            pop_u32(tx);  // bytes_rx
            pop_u32(tx);  // bytes_tx
            for (int i = 0; i < 13; i++) pop_u64(tx);
            for (int i = 0; i < 10; i++) pop_u32(tx);
            pop_u16(tx);
            pop_u16(tx);
            pop_u32(tx);
            assert(version == TIMING_SUMMARY_VERSION);
            r.timing_count++;
        } else if (msg_type == MSG_ERROR) {
            const uint8_t err = pop_byte(tx);
            std::cerr << "  [ERROR frame] code=0x" << std::hex << (int)err << std::dec << "\n";
        }
    }
    return r;
}

static void send_inner_batches(ByteStream& rx, uint32_t first_key, uint32_t count,
                               uint32_t blk_base, uint16_t offno) {
    FpgaTuple32 batch[RX_BUF_TUPLES];
    uint32_t sent = 0u;
    while (sent < count) {
        const uint32_t n = std::min(RX_BUF_TUPLES, count - sent);
        for (uint32_t i = 0u; i < n; i++) {
            const uint32_t key = first_key + sent + i;
            batch[i].key = (int32_t)key;
            batch[i].tid = {blk_base + key, offno};
        }
        send_inner_data(rx, batch, (uint16_t)n);
        sent += n;
    }
}

static uint64_t result_key(const ResultPair& rp) {
    return ((uint64_t)rp.inner_tid.blkno << 32u) | rp.outer_tid.blkno;
}

// ─── Kernel wrapper ───────────────────────────────────────────────────────────

static void run_kernel(ByteStream& rx, ByteStream& tx,
                       uint32_t& status, uint32_t& result_count,
                       uint32_t& error_code, uint32_t& ht_cap, uint32_t& rx_buf_cap) {
    hash_join_kernel(rx, tx, status, result_count, error_code,
                     ht_cap, rx_buf_cap,
                     0u, TIMING_DEFAULT_CLOCK_HZ,
                     0u, 0u, 0u, 0u, 0u, 0u, 0u,
                     0ull, 0ull, 0ull, 0ull, 0ull, 0ull,
                     0ull, 0ull, 0ull, 0ull, 0ull,
                     g_sim_ddr2);
}

// ─── Test cases ───────────────────────────────────────────────────────────────

// TC-1: Simple 3-row inner join with 2 expected matches
static void test_simple_join() {
    std::cout << "[TC-1] simple_join ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    // Inner: keys 10, 20, 30  (blkno = key, offno = 1)
    FpgaTuple32 inner[3] = {
        {10, {10u, 1u}},
        {20, {20u, 1u}},
        {30, {30u, 1u}},
    };
    // Outer: keys 10, 25, 30, 40  — expect matches on 10 and 30
    FpgaTuple32 outer[4] = {
        {10, {100u, 1u}},
        {25, {101u, 1u}},
        {30, {102u, 1u}},
        {40, {103u, 1u}},
    };

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, 3u, 4u);
    send_inner_data(rx, inner, 3u);
    send_outer_data(rx, outer, 4u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == 2u);
    assert(error_code   == ERR_NONE);
    assert(ht_cap       == HT_MAX_ROWS);

    auto dr = drain_tx(tx);
    assert(dr.result_count == 2u);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS (matches=" << result_count << ")\n";
}

// TC-2: No matches (outer keys disjoint from inner)
static void test_no_match() {
    std::cout << "[TC-2] no_match ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    FpgaTuple32 inner[2] = {{1, {1u, 1u}}, {2, {2u, 1u}}};
    FpgaTuple32 outer[2] = {{3, {3u, 1u}}, {4, {4u, 1u}}};

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, 2u, 2u);
    send_inner_data(rx, inner, 2u);
    send_outer_data(rx, outer, 2u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == 0u);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.result_count == 0u);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS\n";
}

// TC-3: Empty inner table — every outer probe should miss
static void test_empty_inner() {
    std::cout << "[TC-3] empty_inner ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    FpgaTuple32 outer[3] = {{5, {5u, 1u}}, {6, {6u, 1u}}, {7, {7u, 1u}}};

    // inner_count=0: skip build phase entirely
    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, 0u, 3u);
    send_outer_data(rx, outer, 3u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == 0u);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS\n";
}

// TC-4: Empty outer table — probe phase should finish without waiting for data
static void test_empty_outer() {
    std::cout << "[TC-4] empty_outer ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    FpgaTuple32 inner[2] = {{1, {1u, 1u}}, {2, {2u, 1u}}};

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, 2u, 0u);
    send_inner_data(rx, inner, 2u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == 0u);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.result_count == 0u);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS\n";
}

// TC-5: CONFIGURE with inner_count > HT_MAX_ROWS should return ERR_OVERFLOW
static void test_overflow_detection() {
    std::cout << "[TC-5] overflow_detection ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, HT_MAX_ROWS + 1u, 0u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(error_code == ERR_OVERFLOW);

    // TX should have an ERROR frame: [MSG_ERROR][count=1][ERR_OVERFLOW]
    assert(!tx.empty());
    const uint8_t msg_type = (uint8_t)tx.read().data;
    assert(msg_type == MSG_ERROR);
    pop_u16(tx);        // count field (1)
    pop_byte(tx);       // error code byte (ERR_OVERFLOW)
    drain_tx(tx);       // drain any trailing debug frames from the next session start

    std::cout << "PASS\n";
}

// TC-6: RESET before CONFIGURE — kernel should ACK and return idle
static void test_reset_before_configure() {
    std::cout << "[TC-6] reset_before_configure ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    push_byte(rx, MSG_RESET);
    push_u16(rx, 0u, /*last=*/true);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status == PHASE_IDLE);
    assert(!tx.empty());  // should have sent ACK
    drain_tx(tx);         // drain ACK + any trailing debug frames

    std::cout << "PASS\n";
}

// TC-7: All-match join (every outer key matches an inner key)
static void test_all_match() {
    std::cout << "[TC-7] all_match ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    const int N = 8;
    FpgaTuple32 inner[N], outer[N];
    for (int i = 0; i < N; i++) {
        inner[i] = {i + 1, {(uint32_t)(i + 1), 1u}};
        outer[i] = {i + 1, {(uint32_t)(i + 100), 1u}};
    }

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, (uint32_t)N, (uint32_t)N);
    send_inner_data(rx, inner, (uint16_t)N);
    send_outer_data(rx, outer, (uint16_t)N);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == (uint32_t)N);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.result_count == (uint32_t)N);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS (matches=" << result_count << ")\n";
}

// TC-8: Explicit zero-count OUTER_DATA terminates probe even if configured
// outer_count is larger than the actual streamed rows.
static void test_explicit_probe_end_marker() {
    std::cout << "[TC-8] explicit_probe_end_marker ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    FpgaTuple32 inner[3] = {
        {10, {1u, 1u}},
        {20, {1u, 2u}},
        {30, {1u, 3u}},
    };
    FpgaTuple32 outer[3] = {
        {99, {2u, 1u}},
        {20, {2u, 2u}},
        {10, {2u, 3u}},
    };

    send_configure(rx, (uint8_t)ALGORITHM_A, KEY_INT32, 256u, 3u, 1024u);
    send_inner_data(rx, inner, 3u);
    send_outer_data(rx, outer, 3u);
    send_outer_data(rx, nullptr, 0u);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == 2u);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.result_count == 2u);
    assert(dr.got_done);
    assert(dr.timing_count == 1u);

    std::cout << "PASS\n";
}

// TC-9: Algorithm B (Grace Hash Join) correctness with K=2 partitions
//
// inner_count = HT_MAX_ROWS + 4 forces K = ceil(12292 / 12288) = 2.
// Inner keys are 1..12292 (unique). Outer probes 8 keys that are all
// present in inner — expects exactly 8 matches.
static void test_grace_basic() {
    std::cout << "[TC-8] grace_basic (K=2 partitions) ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    const uint32_t INNER_N = HT_MAX_ROWS + 4u;  // 12292 → K = 2
    const uint32_t OUTER_N = 8u;

    // These outer probe keys are spread so that some fall in each partition
    FpgaTuple32 outer_probes[8] = {
        {    1, {200u, 1u}},
        {    2, {201u, 1u}},
        {    3, {202u, 1u}},
        {   10, {203u, 1u}},
        {  100, {204u, 1u}},
        { 1000, {205u, 1u}},
        { 5000, {206u, 1u}},
        {12290, {207u, 1u}},
    };

    send_configure(rx, (uint8_t)ALGORITHM_B, KEY_INT32, 256u, INNER_N, OUTER_N);

    // Push 12292 inner tuples in batches of 256
    {
        FpgaTuple32 batch[256];
        uint32_t sent = 0u;
        while (sent < INNER_N) {
            const uint32_t n = std::min(256u, INNER_N - sent);
            for (uint32_t k = 0u; k < n; k++) {
                const uint32_t key = sent + k + 1u;  // keys 1..12292
                batch[k].key       = (int32_t)key;
                batch[k].tid       = {key, 1u};
            }
            send_inner_data(rx, batch, (uint16_t)n);
            sent += n;
        }
    }

    send_outer_data(rx, outer_probes, (uint16_t)OUTER_N);

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(error_code   == ERR_NONE);
    assert(status       == PHASE_DONE);
    assert(result_count == OUTER_N);

    auto dr = drain_tx(tx);
    assert(dr.result_count == OUTER_N);
    assert(dr.got_done);
    assert(dr.results.size() == OUTER_N);
    for (const ResultPair& rp : dr.results) {
        assert(rp.inner_tid.offno == 1u);
        assert(rp.outer_tid.offno == 1u);
        assert(rp.outer_tid.blkno >= 200u);
        assert(rp.outer_tid.blkno <= 207u);
    }

    std::cout << "PASS (matches=" << result_count << ", K=2)\n";
}

// TC-9: Algorithm B with many partitions and more than one RESULT frame.
//
// This checks the DDR2 partition write/read path, result batching, and concrete
// TID pairing on a case that is larger than Algorithm A capacity.
static void test_grace_large_multi_partition() {
    std::cout << "[TC-9] grace_large_multi_partition ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    const uint32_t INNER_N = HT_MAX_ROWS * 4u + 777u; // K = 5
    const uint32_t OUTER_N = 4096u;                   // forces many RESULT frames
    std::vector<uint64_t> expected;
    expected.reserve(OUTER_N);

    send_configure(rx, (uint8_t)ALGORITHM_B, KEY_INT32, 256u, INNER_N, OUTER_N);
    send_inner_batches(rx, 1u, INNER_N, 100000u, 7u);

    FpgaTuple32 batch[RX_BUF_TUPLES];
    uint32_t sent = 0u;
    while (sent < OUTER_N) {
        const uint32_t n = std::min(RX_BUF_TUPLES, OUTER_N - sent);
        for (uint32_t i = 0u; i < n; i++) {
            const uint32_t row = sent + i;
            const uint32_t key = ((row * 97u) % INNER_N) + 1u;
            batch[i].key = (int32_t)key;
            batch[i].tid = {200000u + row, 3u};
            expected.push_back(((uint64_t)(100000u + key) << 32u) | (200000u + row));
        }
        send_outer_data(rx, batch, (uint16_t)n);
        sent += n;
    }

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(status       == PHASE_DONE);
    assert(result_count == OUTER_N);
    assert(error_code   == ERR_NONE);

    auto dr = drain_tx(tx);
    assert(dr.result_count == OUTER_N);
    assert(dr.got_done);
    assert(dr.results.size() == OUTER_N);

    std::vector<uint64_t> got;
    got.reserve(dr.results.size());
    for (const ResultPair& rp : dr.results) {
        assert(rp.inner_tid.offno == 7u);
        assert(rp.outer_tid.offno == 3u);
        got.push_back(result_key(rp));
    }

    std::sort(got.begin(), got.end());
    std::sort(expected.begin(), expected.end());
    assert(got == expected);

    std::cout << "PASS (matches=" << result_count << ", K=5)\n";
}

// TC-10: Algorithm B must fail explicitly when outer partition skew exceeds
// the allocated DDR2 slot instead of overwriting the next partition.
static void test_grace_outer_skew_overflow() {
    std::cout << "[TC-10] grace_outer_skew_overflow ... ";

    ByteStream rx("rx"), tx("tx");
    uint32_t status, result_count, error_code, ht_cap, rx_buf_cap;

    const uint32_t INNER_N = HT_MAX_ROWS * 4u + 1u; // K = 5
    const uint32_t OUTER_N = 256u;                  // per-partition cap is about 104

    send_configure(rx, (uint8_t)ALGORITHM_B, KEY_INT32, 256u, INNER_N, OUTER_N);
    send_inner_batches(rx, 1u, INNER_N, 300000u, 9u);

    FpgaTuple32 batch[RX_BUF_TUPLES];
    uint32_t sent = 0u;
    while (sent < OUTER_N) {
        const uint32_t n = std::min(RX_BUF_TUPLES, OUTER_N - sent);
        for (uint32_t i = 0u; i < n; i++) {
            batch[i].key = 1;
            batch[i].tid = {400000u + sent + i, 1u};
        }
        send_outer_data(rx, batch, (uint16_t)n);
        sent += n;
    }

    run_kernel(rx, tx, status, result_count, error_code, ht_cap, rx_buf_cap);

    assert(error_code == ERR_OVERFLOW);

    bool saw_overflow = false;
    for (int f = 0; f < 10000 && !tx.empty(); f++) {
        const uint8_t msg_type = (uint8_t)tx.read().data;
        const uint16_t count = pop_u16(tx);
        if (msg_type == MSG_ERROR) {
            assert(count == 1u);
            saw_overflow = (pop_byte(tx) == ERR_OVERFLOW);
            break;
        }
        if (msg_type == MSG_ACK || msg_type == MSG_STATUS) {
            for (int i = 0; i < 12; i++) pop_byte(tx);
        } else if (msg_type == MSG_RESULT) {
            for (uint16_t i = 0; i < count; i++) {
                pop_u32(tx); pop_u16(tx);
                pop_u32(tx); pop_u16(tx);
            }
        }
    }
    assert(saw_overflow);

    std::cout << "PASS\n";
}

// ─── main ─────────────────────────────────────────────────────────────────────

int main() {
    std::cout << "=== hash_join_kernel csim testbench ===\n";

    test_simple_join();
    test_no_match();
    test_empty_inner();
    test_empty_outer();
    test_overflow_detection();
    test_reset_before_configure();
    test_all_match();
    test_explicit_probe_end_marker();
    test_grace_basic();
    test_grace_large_multi_partition();
    test_grace_outer_skew_overflow();

    std::cout << "\nAll tests passed.\n";
    return 0;
}
