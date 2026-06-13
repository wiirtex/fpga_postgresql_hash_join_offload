#pragma once
#include <stdint.h>

// ─── Constants ───────────────────────────────────────────────────────────────

static const uint32_t HT_LOG2_SLOTS  = 14u;
static const uint32_t HT_SLOTS       = 1u << HT_LOG2_SLOTS;  // 16 384 slots
static const uint32_t HT_MAX_ROWS    = HT_SLOTS * 3u / 4u;   // 12 288 rows (75% load)
static const uint32_t MAX_PROBE_DIST = 16u;   // max linear probe steps before giving up
static const uint32_t RX_BUF_TUPLES  = 256u;  // max tuples parsed from one protocol frame
static const uint32_t RX_CREDIT_TUPLES = 512u; // advertised flow-control credit (tuples)
static const uint32_t TX_BUF_SIZE    = 256u;  // result batch buffer (per outer batch)
static const uint32_t RESULT_FLUSH_PAIRS = 118u; // 3 + 118 * 12 = 1419 bytes, below Ethernet MTU
static const uint16_t ACK_BATCH_FRAMES = 2u;  // Algorithm A ACK period for DATA frames

// ─── Algorithm B: Grace Hash Join constants ───────────────────────────────────

// GRACE_WORDS_PER_TUPLE: each tuple stored in DDR2 as 2 × int64_t words:
//   word[0] = key (int64_t; int32 keys sign-extended before writing)
//   word[1] = ((uint64_t)tid.blkno << 32) | ((uint64_t)tid.offno << 16)
// 16 bytes/tuple, 8-byte aligned — burst-friendly for MIG AXI Master.
static const uint32_t GRACE_WORDS_PER_TUPLE  = 2u;

// GRACE_MAX_K = 256: inner up to 3.1M rows (256 partitions × 12288 rows/partition)
static const uint32_t GRACE_MAX_K            = 256u;

// Inner slot in DDR2 is exactly HT_MAX_ROWS tuples per partition (must fit in BRAM for Build phase)
static const uint32_t GRACE_INNER_SLOT_WORDS = HT_MAX_ROWS * GRACE_WORDS_PER_TUPLE;  // 24576 words = 192 KB

// Base address of outer area in DDR2 (in words): after all inner slots
static const uint32_t GRACE_OUTER_AREA_BASE  = GRACE_MAX_K * GRACE_INNER_SLOT_WORDS; // 6291456 words = 48 MB

// Total DDR2 capacity: 128 MB / 8 bytes per word = 16777216 words
static const uint32_t GRACE_DDR2_TOTAL_WORDS = 16u * 1024u * 1024u;                  // 16777216 words = 128 MB

// Max inner rows for Algorithm B (GRACE_MAX_K partitions × HT_MAX_ROWS rows each)
static const uint32_t GRACE_MAX_INNER_ROWS   = GRACE_MAX_K * HT_MAX_ROWS;            // 3145728 ≈ 3.1M

// ─── Algorithm selector ───────────────────────────────────────────────────────

enum AlgorithmType : uint8_t {
    ALGORITHM_A = 0x00,  // Linear Probing Hash Join (BRAM-only, inner ≤ HT_MAX_ROWS)
    ALGORITHM_B = 0x01,  // Grace Hash Join (BRAM + DDR2, inner ≤ GRACE_MAX_INNER_ROWS)
};

// ─── Protocol enumerations ───────────────────────────────────────────────────

// Wire message types (1-byte tag in every frame header)
// Direction: PG = PostgreSQL → FPGA, FP = FPGA → PostgreSQL
enum MsgType : uint8_t {
    MSG_CONFIGURE  = 0x01,  // PG: begin new join session
    MSG_INNER_DATA = 0x02,  // PG: batch of inner-table tuples (build phase)
    MSG_OUTER_DATA = 0x03,  // PG: batch of outer-table tuples (probe phase)
    MSG_RESULT     = 0x04,  // FP: batch of matched TID pairs
    MSG_ACK        = 0x05,  // FP: batch acknowledgement + credit update
    MSG_STATUS     = 0x06,  // FP: heartbeat with current phase and counters
    MSG_ERROR      = 0x07,  // FP: fatal error, session aborted
    MSG_RESET      = 0x08,  // PG: abort session and return to IDLE
    MSG_DEBUG      = 0x09,  // FP: debug event (level + code + value, 7-byte payload)
    MSG_TIMING     = 0x0A,  // FP: optional Algorithm A timing summary
};

// MSG_TIMING payload version for the first Algorithm A timing summary format.
// The frame uses count=1 and a manually serialized little-endian payload:
//
//   u16 version, u16 flags, u32 clock_hz
//   u32 inner_rows, outer_rows, matched_rows
//   u32 inner_frames, outer_frames, result_frames, ack_frames, debug_frames
//   u32 bytes_rx, bytes_tx
//   u64 session_total_cycles, config_cycles
//   u64 build_rx_cycles, build_compute_cycles, build_total_cycles
//   u64 probe_rx_cycles, probe_compute_cycles, result_emit_cycles
//   u64 probe_total_cycles, ack_emit_cycles
//   u64 rx_wait_cycles, tx_blocked_cycles, protocol_wait_cycles
//   u32 max_build_batch_cycles, max_probe_batch_cycles, max_result_frame_cycles
//   u32 hash_build_inserts, hash_probe_lookups, hash_probe_hits
//   u32 hash_probe_misses, hash_overflow_errors
//   u32 hash_build_collision_steps, hash_probe_collision_steps
//   u16 hash_max_build_probe_distance, hash_max_probe_distance
//   u32 hash_table_load_factor_ppm
//
// Total payload size: 200 bytes.  The struct below documents the logical
// fields only; host/kernel code should serialize manually to avoid padding.
static const uint16_t TIMING_SUMMARY_VERSION = 1u;
static const uint16_t TIMING_SUMMARY_COUNT   = 1u;
static const uint16_t TIMING_SUMMARY_BYTES   = 200u;
static const uint16_t TIMING_FLAG_ESTIMATED_CYCLES = 0x0001u;
static const uint16_t TIMING_FLAG_RTL_STREAM_CYCLES = 0x0002u;
static const uint32_t TIMING_DEFAULT_CLOCK_HZ = 70000000u;

// Error codes carried in MSG_ERROR frames and the error_code AXI-Lite register
enum FpgaError : uint8_t {
    ERR_NONE          = 0x00,  // no error
    ERR_OVERFLOW      = 0x01,  // hash table full (inner_count > HT_MAX_ROWS)
    ERR_INVALID_CMD   = 0x02,  // unknown msg_type received
    ERR_BAD_STATE     = 0x03,  // valid msg_type but wrong FSM phase
    ERR_DDR_FAULT     = 0x04,  // DDR2 access error (Algorithm B only)
    ERR_DUPLICATE_KEY = 0x05,  // duplicate key in inner table (MVP: not supported)
};

// Kernel FSM phases — written to the status AXI-Lite register
enum Phase : uint8_t {
    PHASE_IDLE     = 0,  // waiting for CONFIGURE
    PHASE_BUILDING = 1,  // receiving INNER_DATA, building hash table
    PHASE_PROBING  = 2,  // receiving OUTER_DATA, emitting RESULT
    PHASE_DONE     = 3,  // join complete, result_count is valid
};

// Join key type — carried in ConfigurePayload
enum KeyType : uint8_t {
    KEY_INT32 = 0x01,  // PostgreSQL int4, OID 23  — 10-byte FpgaTuple on wire
    KEY_INT64 = 0x02,  // PostgreSQL int8, OID 20  — 14-byte FpgaTuple on wire
};

// ─── Wire data structures ─────────────────────────────────────────────────────
//
// All multi-byte integers are little-endian on the wire.
// Structs below document the wire layout; actual (de)serialization is manual
// (byte-by-byte via AXI-Stream) to avoid compiler padding surprises.

// PostgreSQL Tuple Identifier — matches ItemPointerData layout
struct Tid {
    uint32_t blkno;  // block number (4 bytes)
    uint16_t offno;  // item offset within block (2 bytes)
};

// Tuple transmitted PG → FPGA when key is int32  (10 bytes on wire)
struct FpgaTuple32 {
    int32_t key;
    Tid     tid;
};

// Tuple transmitted PG → FPGA when key is int64  (14 bytes on wire)
struct FpgaTuple64 {
    int64_t key;
    Tid     tid;
};

// Join result returned FPGA → PG               (12 bytes on wire)
struct ResultPair {
    Tid inner_tid;
    Tid outer_tid;
};

// CONFIGURE payload                             (13 bytes on wire)
struct ConfigurePayload {
    uint8_t  algorithm;    // 0 = LinearProbing (Algorithm A), 1 = GraceHJ (Algorithm B)
    uint8_t  key_type;     // KEY_INT32 or KEY_INT64
    uint16_t rx_buf_slots; // host hint for FPGA RX buffer size (we advertise our own)
    uint32_t inner_count;  // expected inner rows; must be <= HT_MAX_ROWS for Algorithm A
    uint32_t outer_count;  // expected outer rows; 0 = unknown
    uint8_t  session_id;   // host-generated session tag echoed in ACK/STATUS frames
};

// STATUS / ACK payload                          (12 bytes on wire)
struct StatusPayload {
    uint8_t  phase;           // current Phase
    uint8_t  session_id;      // echoes ConfigurePayload.session_id; 0 for reset/idle watchdog
    uint16_t credit;          // RX buffer slots available right now
    uint32_t rows_processed;  // outer rows probed so far
    uint32_t rows_matched;    // result pairs emitted so far
};

// Logical MSG_TIMING summary payload.  Manual serialization order is documented
// above; do not transmit this struct with sizeof()/memcpy().
struct TimingSummaryPayload {
    uint16_t version;
    uint16_t flags;
    uint32_t clock_hz;

    uint32_t inner_rows;
    uint32_t outer_rows;
    uint32_t matched_rows;

    uint32_t inner_frames;
    uint32_t outer_frames;
    uint32_t result_frames;
    uint32_t ack_frames;
    uint32_t debug_frames;

    uint32_t bytes_rx;
    uint32_t bytes_tx;

    uint64_t session_total_cycles;
    uint64_t config_cycles;
    uint64_t build_rx_cycles;
    uint64_t build_compute_cycles;
    uint64_t build_total_cycles;
    uint64_t probe_rx_cycles;
    uint64_t probe_compute_cycles;
    uint64_t result_emit_cycles;
    uint64_t probe_total_cycles;
    uint64_t ack_emit_cycles;
    uint64_t rx_wait_cycles;
    uint64_t tx_blocked_cycles;
    uint64_t protocol_wait_cycles;

    uint32_t max_build_batch_cycles;
    uint32_t max_probe_batch_cycles;
    uint32_t max_result_frame_cycles;

    uint32_t hash_build_inserts;
    uint32_t hash_probe_lookups;
    uint32_t hash_probe_hits;
    uint32_t hash_probe_misses;
    uint32_t hash_overflow_errors;
    uint32_t hash_build_collision_steps;
    uint32_t hash_probe_collision_steps;
    uint16_t hash_max_build_probe_distance;
    uint16_t hash_max_probe_distance;
    uint32_t hash_table_load_factor_ppm;
};
