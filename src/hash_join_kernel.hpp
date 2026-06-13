#pragma once
#include "hash_join_types.hpp"
#include "ap_axi_sdata.h"
#include "hls_stream.h"

// AXI-Stream byte-level types used by the kernel
typedef ap_axiu<8, 0, 0, 0> AxisByte;
typedef hls::stream<AxisByte> ByteStream;

// ─── AXI-Stream read helpers ──────────────────────────────────────────────────
// Moved here so hash_join_grace.hpp can use them without re-defining.

static inline uint8_t rx_byte(ByteStream& s) {
    return (uint8_t)s.read().data;
}

static inline uint16_t rx_u16(ByteStream& s) {
    const uint16_t b0 = s.read().data;
    const uint16_t b1 = s.read().data;
    return b0 | (uint16_t)(b1 << 8u);
}

static inline uint32_t rx_u32(ByteStream& s) {
    const uint32_t b0 = s.read().data;
    const uint32_t b1 = s.read().data;
    const uint32_t b2 = s.read().data;
    const uint32_t b3 = s.read().data;
    return b0 | (b1 << 8u) | (b2 << 16u) | (b3 << 24u);
}

static inline int64_t rx_i64(ByteStream& s) {
    const uint64_t b0 = s.read().data;
    const uint64_t b1 = s.read().data;
    const uint64_t b2 = s.read().data;
    const uint64_t b3 = s.read().data;
    const uint64_t b4 = s.read().data;
    const uint64_t b5 = s.read().data;
    const uint64_t b6 = s.read().data;
    const uint64_t b7 = s.read().data;
    return (int64_t)(b0        | (b1 << 8u)  | (b2 << 16u) | (b3 << 24u) |
                     (b4 << 32u) | (b5 << 40u) | (b6 << 48u) | (b7 << 56u));
}

// Read a 6-byte TID (blkno u32 + offno u16)
static inline Tid rx_tid(ByteStream& s) {
    Tid t;
    t.blkno = rx_u32(s);
    t.offno  = rx_u16(s);
    return t;
}

static inline void rx_discard_tuple(ByteStream& s, KeyType keytype) {
#pragma HLS INLINE
    if (keytype == KEY_INT64) {
        (void)rx_i64(s);
    } else {
        (void)rx_u32(s);
    }
    (void)rx_tid(s);
}

// ─── AXI-Stream write helpers ─────────────────────────────────────────────────

static inline void tx_byte(ByteStream& s, uint8_t v, bool last = false) {
    AxisByte b;
    b.data = v;
    b.last = last ? 1u : 0u;
    b.keep = 0xFFu;
    s.write(b);
}

static inline void tx_u16(ByteStream& s, uint16_t v, bool last = false) {
    tx_byte(s,  v & 0xFFu);
    tx_byte(s, (v >> 8u) & 0xFFu, last);
}

static inline void tx_u32(ByteStream& s, uint32_t v, bool last = false) {
    tx_byte(s, (v >>  0u) & 0xFFu);
    tx_byte(s, (v >>  8u) & 0xFFu);
    tx_byte(s, (v >> 16u) & 0xFFu);
    tx_byte(s, (v >> 24u) & 0xFFu, last);
}

static inline void tx_u64(ByteStream& s, uint64_t v, bool last = false) {
    tx_u32(s, (uint32_t)(v & 0xFFFFFFFFull));
    tx_u32(s, (uint32_t)(v >> 32u), last);
}

// ─── Protocol frame builders ──────────────────────────────────────────────────

// Every outgoing frame starts with [msg_type:1B][count:2B]
static inline void tx_header(ByteStream& tx, MsgType type, uint16_t count) {
    tx_byte(tx, (uint8_t)type);
    tx_u16(tx, count);
}

// MSG_ACK / MSG_STATUS — same 12-byte payload (StatusPayload)
static inline void tx_status_frame(ByteStream& tx, MsgType type, Phase phase,
                                    uint32_t processed, uint32_t matched,
                                    uint16_t credit, uint8_t session_id = 0u) {
    tx_header(tx, type, 1u);
    tx_byte(tx, (uint8_t)phase);
    tx_byte(tx, session_id);
    tx_u16(tx, credit);
    tx_u32(tx, processed);
    tx_u32(tx, matched, /*last=*/true);
}

// MSG_ERROR — single error-code byte as payload
static inline void tx_error(ByteStream& tx, FpgaError err) {
    tx_header(tx, MSG_ERROR, 1u);
    tx_byte(tx, (uint8_t)err, /*last=*/true);
}

// MSG_RESULT — flush count ResultPairs from buf to tx.
// count must be <= TX_BUF_SIZE.
static inline void tx_results(ByteStream& tx,
                               const ResultPair buf[TX_BUF_SIZE],
                               uint16_t count) {
    if (count == 0u) return;
    tx_header(tx, MSG_RESULT, count);
    for (uint16_t i = 0u; i < count; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
        const bool last = (i == (uint16_t)(count - 1u));
        tx_u32(tx, buf[i].inner_tid.blkno);
        tx_u16(tx, buf[i].inner_tid.offno);
        tx_u32(tx, buf[i].outer_tid.blkno);
        tx_u16(tx, buf[i].outer_tid.offno, last);
    }
}

static inline void tx_timing_summary(ByteStream& tx,
                                     const TimingSummaryPayload& t) {
    tx_header(tx, MSG_TIMING, TIMING_SUMMARY_COUNT);
    tx_u16(tx, t.version);
    tx_u16(tx, t.flags);
    tx_u32(tx, t.clock_hz);
    tx_u32(tx, t.inner_rows);
    tx_u32(tx, t.outer_rows);
    tx_u32(tx, t.matched_rows);
    tx_u32(tx, t.inner_frames);
    tx_u32(tx, t.outer_frames);
    tx_u32(tx, t.result_frames);
    tx_u32(tx, t.ack_frames);
    tx_u32(tx, t.debug_frames);
    tx_u32(tx, t.bytes_rx);
    tx_u32(tx, t.bytes_tx);
    tx_u64(tx, t.session_total_cycles);
    tx_u64(tx, t.config_cycles);
    tx_u64(tx, t.build_rx_cycles);
    tx_u64(tx, t.build_compute_cycles);
    tx_u64(tx, t.build_total_cycles);
    tx_u64(tx, t.probe_rx_cycles);
    tx_u64(tx, t.probe_compute_cycles);
    tx_u64(tx, t.result_emit_cycles);
    tx_u64(tx, t.probe_total_cycles);
    tx_u64(tx, t.ack_emit_cycles);
    tx_u64(tx, t.rx_wait_cycles);
    tx_u64(tx, t.tx_blocked_cycles);
    tx_u64(tx, t.protocol_wait_cycles);
    tx_u32(tx, t.max_build_batch_cycles);
    tx_u32(tx, t.max_probe_batch_cycles);
    tx_u32(tx, t.max_result_frame_cycles);
    tx_u32(tx, t.hash_build_inserts);
    tx_u32(tx, t.hash_probe_lookups);
    tx_u32(tx, t.hash_probe_hits);
    tx_u32(tx, t.hash_probe_misses);
    tx_u32(tx, t.hash_overflow_errors);
    tx_u32(tx, t.hash_build_collision_steps);
    tx_u32(tx, t.hash_probe_collision_steps);
    tx_u16(tx, t.hash_max_build_probe_distance);
    tx_u16(tx, t.hash_max_probe_distance);
    tx_u32(tx, t.hash_table_load_factor_ppm, /*last=*/true);
}

// ─── MSG_DEBUG frame emitter ──────────────────────────────────────────────────
// emit_debug() sends a 10-byte MSG_DEBUG frame:
//   [0x09][0x01][0x00]  header (3 bytes)
//   [level:1B][code:2B LE][value:4B LE]  payload (7 bytes)
//
// Guard: define __SYNTHESIS_DISABLE_DEBUG__ to compile it out completely.
// Safe to call between top-level loop iterations (NOT inside tight inner loops).

// DbgLevel / DbgCode mirrors: keep in sync with host/include/debug_codes.hpp.
enum DbgLevelKernel : uint8_t  { DBG_DEBUG=0, DBG_INFO=1, DBG_WARN=2, DBG_ERROR=3 };
enum DbgCodeKernel  : uint16_t {
    DBG_IDLE_ENTER   = 0x0001,
    DBG_CONFIGURE    = 0x0002,
    DBG_BUILD_BATCH  = 0x0003,
    DBG_BUILD_DONE   = 0x0004,
    DBG_PROBE_BATCH  = 0x0005,
    DBG_PROBE_DONE   = 0x0006,
    DBG_RESET        = 0x0007,
    DBG_BUILD_KEY    = 0x0008,
    DBG_PROBE_KEY    = 0x0009,
    DBG_PROBE_HIT    = 0x000A,
    DBG_KERNEL_ERROR = 0x00FF,
};

// Active only during RTL synthesis (__SYNTHESIS__ defined by Vitis HLS).
// During C simulation __SYNTHESIS__ is not defined, so the testbench never
// sees unexpected MSG_DEBUG frames and csim passes cleanly.
// Also disabled when __SYNTHESIS_DISABLE_DEBUG__ is explicitly defined.
#if defined(__SYNTHESIS__) && !defined(__SYNTHESIS_DISABLE_DEBUG__)
static inline void emit_debug(ByteStream& tx,
                               DbgLevelKernel level,
                               DbgCodeKernel  code,
                               uint32_t       value) {
    tx_header(tx, MSG_DEBUG, 1u);          // [0x09][0x01][0x00]
    tx_byte(tx, (uint8_t)level);           // level
    tx_u16(tx, (uint16_t)code);            // code LE
    tx_u32(tx, value, /*last=*/true);      // value LE
}
#else
static inline void emit_debug(ByteStream&, DbgLevelKernel, DbgCodeKernel, uint32_t) {}
#endif

// ─── Frame-boundary watchdog read ────────────────────────────────────────────
//
// rx_byte_guarded — non-blocking poll with a cycle counter.
// Use ONLY at frame boundaries (reading the first byte of a new frame), never
// inside pipelined inner_batch / outer_batch loops.
//
// On timeout (~5 s at 70 MHz): emits MSG_DEBUG(WARN, DBG_RESET) + MSG_ACK(IDLE),
// sets session_done=true, and returns 0.  The caller must skip the rest of the
// frame parse when session_done is true.

// ~5 seconds at 70 MHz
static const uint32_t RX_WATCHDOG_CYCLES = 350000000u;

static inline uint8_t rx_byte_guarded(ByteStream& rx, ByteStream& tx,
                                       bool& session_done,
                                       uint8_t session_id = 0u) {
#pragma HLS INLINE
    uint32_t ctr = 0u;
    rx_guard_loop:
    while (true) {
#pragma HLS PIPELINE II=1
        AxisByte b;
        if (rx.read_nb(b)) return (uint8_t)b.data;
        if (++ctr >= RX_WATCHDOG_CYCLES) {
            emit_debug(tx, DBG_WARN, DBG_RESET, ctr);
            tx_status_frame(tx, MSG_ACK, PHASE_IDLE, 0u, 0u,
                            RX_CREDIT_TUPLES, session_id);
            session_done = true;
            return 0u;
        }
    }
}

// ─── hash_join_kernel ─────────────────────────────────────────────────────────
//
// Algorithms A and B: Linear Probing Hash Join (BRAM-only) and Grace Hash Join
// (BRAM + DDR2). Processes one complete join session per invocation:
//   CONFIGURE → BUILD (INNER_DATA) → PROBE (OUTER_DATA ↔ RESULT) → DONE
//
// AXI-Stream interfaces:
//   rx  — incoming byte stream from host (wire protocol frames)
//   tx  — outgoing byte stream to host   (ACK / RESULT / STATUS / ERROR frames)
//
// AXI-Lite registers (host-visible, little-endian):
//   status_reg       [R]  current Phase value (0=idle … 3=done)
//   result_count_reg [R]  total ResultPairs emitted (valid when status==DONE)
//   error_code_reg   [R]  last FpgaError (0 = no error)
//   ht_capacity_reg  [R]  HT_MAX_ROWS constant  (12 288) — max inner rows for Algorithm A
//   rx_buf_cap_reg   [R]  RX_CREDIT_TUPLES constant       — advertised input credit
//
// RTL timing monitor inputs:
//   Optional passive stream timestamps from axis_protocol_timing_monitor.
//   All zeros mean "monitor absent"; the kernel then emits HLS-estimated cycles.
//
// AXI Master:
//   ddr2_buf         [RW] DDR2 SDRAM buffer for Algorithm B (unused by Algorithm A)

void hash_join_kernel(
    ByteStream&        rx,
    ByteStream&        tx,
    volatile uint32_t& status_reg,
    volatile uint32_t& result_count_reg,
    volatile uint32_t& error_code_reg,
    volatile uint32_t& ht_capacity_reg,
    volatile uint32_t& rx_buf_cap_reg,
    uint32_t           rtl_timing_valid,
    uint32_t           rtl_timing_clock_hz,
    uint32_t           rtl_inner_frames,
    uint32_t           rtl_outer_frames,
    uint32_t           rtl_result_frames,
    uint32_t           rtl_ack_frames,
    uint32_t           rtl_debug_frames,
    uint32_t           rtl_bytes_rx,
    uint32_t           rtl_bytes_tx,
    uint64_t           rtl_cycle_counter,
    uint64_t           rtl_ts_config_first,
    uint64_t           rtl_ts_config_ack,
    uint64_t           rtl_ts_build_first,
    uint64_t           rtl_ts_build_last,
    uint64_t           rtl_ts_build_ack_last,
    uint64_t           rtl_ts_probe_first,
    uint64_t           rtl_ts_probe_last,
    uint64_t           rtl_ts_probe_ack_last,
    uint64_t           rtl_ts_result_first,
    uint64_t           rtl_ts_result_last,
    int64_t*           ddr2_buf
);
