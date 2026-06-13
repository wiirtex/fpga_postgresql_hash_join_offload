#pragma once
// Minimal test framework — no external dependencies.
#include <cstdio>
#include <cstdint>
#include <vector>
#include "fpga_types.hpp"

// ─── Assertion macros ─────────────────────────────────────────────────────────

inline int g_failures = 0;

#define CHECK(cond)                                                           \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                      \
        }                                                                      \
    } while (0)

#define CHECK_EQ(a, b)  CHECK((a) == (b))
#define CHECK_NEQ(a, b) CHECK((a) != (b))

#define TEST_RESULT() \
    (g_failures == 0                                                           \
        ? (std::printf("PASS  (%s)\n", __FILE__), 0)                          \
        : (std::printf("FAIL  %d assertion(s) failed  (%s)\n",                \
                        g_failures, __FILE__), 1))

// ─── Frame builders — helpers for constructing mock FPGA responses ─────────────

// Build a raw little-endian u16
inline void push_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
// Build a raw little-endian u32
inline void push_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
    v.push_back(static_cast<uint8_t>(x >> 16));
    v.push_back(static_cast<uint8_t>(x >> 24));
}

// Build a raw little-endian u64
inline void push_u64(std::vector<uint8_t>& v, uint64_t x) {
    for (unsigned i = 0; i < 8; ++i)
        v.push_back(static_cast<uint8_t>(x >> (8u * i)));
}

// [MSG_ACK][count=1][StatusPayload]
inline std::vector<uint8_t> make_ack(uint8_t phase, uint16_t credit,
                                      uint32_t processed = 0, uint32_t matched = 0,
                                      uint8_t session_id = 1u) {
    std::vector<uint8_t> f;
    f.push_back(MSG_ACK);
    push_u16(f, 1);          // count
    f.push_back(phase);       // phase
    f.push_back(session_id);
    push_u16(f, credit);
    push_u32(f, processed);
    push_u32(f, matched);
    return f;
}

// [MSG_STATUS][count=1][StatusPayload]
inline std::vector<uint8_t> make_status(uint8_t phase, uint32_t matched = 0,
                                        uint8_t session_id = 1u) {
    std::vector<uint8_t> f;
    f.push_back(MSG_STATUS);
    push_u16(f, 1);
    f.push_back(phase);
    f.push_back(session_id);
    push_u16(f, 0);           // credit (not used in STATUS)
    push_u32(f, 0);           // rows_processed
    push_u32(f, matched);
    return f;
}

// [MSG_ERROR][count=1][FpgaError]
inline std::vector<uint8_t> make_error(FpgaError err) {
    return { MSG_ERROR, 0x01, 0x00, static_cast<uint8_t>(err) };
}

// [MSG_RESULT][count=N][N × 12-byte ResultPair]
inline std::vector<uint8_t> make_result(const std::vector<ResultPair>& pairs) {
    std::vector<uint8_t> f;
    f.push_back(MSG_RESULT);
    push_u16(f, static_cast<uint16_t>(pairs.size()));
    for (const auto& rp : pairs) {
        push_u32(f, rp.inner_tid.blkno);
        push_u16(f, rp.inner_tid.offno);
        push_u32(f, rp.outer_tid.blkno);
        push_u16(f, rp.outer_tid.offno);
    }
    return f;
}

// [MSG_TIMING][count=1][TimingSummaryPayload v1]
inline std::vector<uint8_t> make_timing_summary(const TimingSummaryPayload& t) {
    std::vector<uint8_t> f;
    f.push_back(MSG_TIMING);
    push_u16(f, TIMING_SUMMARY_COUNT);

    push_u16(f, t.version);
    push_u16(f, t.flags);
    push_u32(f, t.clock_hz);

    push_u32(f, t.inner_rows);
    push_u32(f, t.outer_rows);
    push_u32(f, t.matched_rows);

    push_u32(f, t.inner_frames);
    push_u32(f, t.outer_frames);
    push_u32(f, t.result_frames);
    push_u32(f, t.ack_frames);
    push_u32(f, t.debug_frames);

    push_u32(f, t.bytes_rx);
    push_u32(f, t.bytes_tx);

    push_u64(f, t.session_total_cycles);
    push_u64(f, t.config_cycles);
    push_u64(f, t.build_rx_cycles);
    push_u64(f, t.build_compute_cycles);
    push_u64(f, t.build_total_cycles);
    push_u64(f, t.probe_rx_cycles);
    push_u64(f, t.probe_compute_cycles);
    push_u64(f, t.result_emit_cycles);
    push_u64(f, t.probe_total_cycles);
    push_u64(f, t.ack_emit_cycles);
    push_u64(f, t.rx_wait_cycles);
    push_u64(f, t.tx_blocked_cycles);
    push_u64(f, t.protocol_wait_cycles);

    push_u32(f, t.max_build_batch_cycles);
    push_u32(f, t.max_probe_batch_cycles);
    push_u32(f, t.max_result_frame_cycles);

    push_u32(f, t.hash_build_inserts);
    push_u32(f, t.hash_probe_lookups);
    push_u32(f, t.hash_probe_hits);
    push_u32(f, t.hash_probe_misses);
    push_u32(f, t.hash_overflow_errors);
    push_u32(f, t.hash_build_collision_steps);
    push_u32(f, t.hash_probe_collision_steps);
    push_u16(f, t.hash_max_build_probe_distance);
    push_u16(f, t.hash_max_probe_distance);
    push_u32(f, t.hash_table_load_factor_ppm);

    CHECK_EQ(f.size(), static_cast<size_t>(3u + TIMING_SUMMARY_BYTES));
    return f;
}
