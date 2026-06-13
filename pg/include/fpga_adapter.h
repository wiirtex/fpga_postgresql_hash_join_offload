#ifndef FPGA_ADAPTER_H
#define FPGA_ADAPTER_H

/*
 * fpga_adapter.h — C bridge between the PostgreSQL extension (C) and the
 * host-side C++17 FpgaClient library.
 *
 * This header is intentionally pure C (C99): no C++ headers, no PostgreSQL
 * headers.  It is included by both:
 *   - fpga_executor.c  (compiled as C with PG headers)
 *   - fpga_adapter.cpp (compiled as C++17 with our host library headers)
 *
 * fpga_adapter.cpp contains static_assert checks that verify the layout of
 * AdapterTid / AdapterInputTuple / AdapterResultPair matches the layout of
 * the corresponding types in host/include/fpga_types.hpp (Tid, InputTuple,
 * ResultPair).  Any mismatch will be caught at compile time.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Mirror types (must match hash_join_types.hpp byte-for-byte) ─────────── */

typedef struct AdapterTid {
    uint32_t blkno;   /* PostgreSQL block number */
    uint16_t offno;   /* item offset within block */
} AdapterTid;

typedef struct AdapterInputTuple {
    int64_t   key;    /* join key; truncated to int32 on wire when key_type=INT32 */
    AdapterTid tid;
} AdapterInputTuple;

typedef struct AdapterResultPair {
    AdapterTid inner_tid;
    AdapterTid outer_tid;
} AdapterResultPair;

/* Must match KeyType enum values in hash_join_types.hpp */
typedef enum AdapterKeyType {
    ADAPTER_KEY_INT32 = 0x01,   /* PostgreSQL int4, OID 23 */
    ADAPTER_KEY_INT64 = 0x02    /* PostgreSQL int8, OID 20 */
} AdapterKeyType;

typedef struct AdapterHostMetrics {
    double config_send_ms;
    double config_ack_wait_ms;
    double build_send_ms;
    double build_ack_wait_ms;
    double probe_send_ms;
    double probe_ack_wait_ms;
    double final_status_wait_ms;
    double reset_wait_ms;
    double result_recv_ms;
    double adapter_total_ms;

    uint64_t protocol_frames_sent;
    uint64_t protocol_frames_recv;
    uint64_t transport_sends;
    uint64_t bytes_sent;
    uint64_t bytes_recv;

    uint64_t config_frames_sent;
    uint64_t inner_frames_sent;
    uint64_t outer_frames_sent;
    uint64_t reset_frames_sent;
    uint64_t ack_frames_recv;
    uint64_t status_frames_recv;
    uint64_t result_frames_recv;
    uint64_t debug_frames_recv;
    uint64_t timing_frames_recv;
    uint64_t error_frames_recv;
    uint64_t result_pairs_recv;

    bool has_board_timing;
    uint32_t board_timing_version;
    uint32_t board_timing_flags;
    uint32_t board_clock_hz;
    uint32_t board_inner_rows;
    uint32_t board_outer_rows;
    uint32_t board_matched_rows;
    uint32_t board_inner_frames;
    uint32_t board_outer_frames;
    uint32_t board_result_frames;
    uint32_t board_ack_frames;
    uint32_t board_debug_frames;
    uint32_t board_bytes_rx;
    uint32_t board_bytes_tx;
    uint64_t board_session_total_cycles;
    uint64_t board_config_cycles;
    uint64_t board_build_rx_cycles;
    uint64_t board_build_compute_cycles;
    uint64_t board_build_total_cycles;
    uint64_t board_probe_rx_cycles;
    uint64_t board_probe_compute_cycles;
    uint64_t board_result_emit_cycles;
    uint64_t board_probe_total_cycles;
    uint64_t board_ack_emit_cycles;
    uint64_t board_rx_wait_cycles;
    uint64_t board_tx_blocked_cycles;
    uint64_t board_protocol_wait_cycles;
    uint32_t board_max_build_batch_cycles;
    uint32_t board_max_probe_batch_cycles;
    uint32_t board_max_result_frame_cycles;
    uint32_t board_hash_build_inserts;
    uint32_t board_hash_probe_lookups;
    uint32_t board_hash_probe_hits;
    uint32_t board_hash_probe_misses;
    uint32_t board_hash_overflow_errors;
    uint32_t board_hash_build_collision_steps;
    uint32_t board_hash_probe_collision_steps;
    uint32_t board_hash_max_build_probe_distance;
    uint32_t board_hash_max_probe_distance;
    uint32_t board_hash_table_load_factor_ppm;
} AdapterHostMetrics;

/* ── Main entry point ────────────────────────────────────────────────────── */

/*
 * fpga_adapter_run — execute a hash join on the FPGA (or simulation).
 *
 * BLOCKING: does not return until all results are received or hard_timeout_ms
 * elapses.  The PostgreSQL backend is blocked during this call.
 *
 * Parameters:
 *   key_type        — ADAPTER_KEY_INT32 or ADAPTER_KEY_INT64
 *   use_simulation  — if true, use SoftwareKernel (no physical FPGA needed)
 *   device          — UART device path (e.g. "/dev/ttyUSB0"), ignored when
 *                     use_simulation=true
 *   device_baud     — UART baud rate, ignored when use_simulation=true
 *   warn_ms         — log a warning if no ACK arrives within this many ms
 *   hard_ms         — abort and return false after this many ms
 *   inner / inner_count — build-phase tuples (inner relation)
 *   outer / outer_count — probe-phase tuples (outer relation)
 *
 * On success:
 *   Returns true.  *out_pairs points to a malloc()'d array of *out_count
 *   AdapterResultPair structs.  The CALLER is responsible for free()'ing
 *   *out_pairs (after copying into palloc'd memory).
 *
 * On failure (FpgaException, FpgaTimeoutException, etc.):
 *   Returns false.  *out_pairs = NULL, *out_count = 0.
 *   Call fpga_adapter_last_error() for a human-readable message.
 */
bool fpga_adapter_run(AdapterKeyType              key_type,
                      bool                        use_simulation,
                      const char                 *algorithm_name,
                      const char                 *transport_name,
                      const char                 *device,
                      uint32_t                    device_baud,
                      uint32_t                    warn_ms,
                      uint32_t                    hard_ms,
                      uint16_t                    max_batch_tuples,
                      uint16_t                    ack_window_frames,
                      const AdapterInputTuple    *inner,
                      size_t                      inner_count,
                      const AdapterInputTuple    *outer,
                      size_t                      outer_count,
                      AdapterResultPair         **out_pairs,
                      size_t                     *out_count);

/* Returns the error message from the most recent failed fpga_adapter_run(). */
const char *fpga_adapter_last_error(void);

/* Copies metrics from the most recent fpga_adapter_run(); returns false if none exist. */
bool fpga_adapter_last_host_metrics(AdapterHostMetrics *out_metrics);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FPGA_ADAPTER_H */
