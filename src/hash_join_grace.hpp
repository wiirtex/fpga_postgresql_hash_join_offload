#pragma once
#include "hash_join_kernel.hpp"
#include "hash_join_linear.hpp"

// ─── GraceHashEngine ──────────────────────────────────────────────────────────
//
// Algorithm B: Grace Hash Join using DDR2 as partition storage.
//
// Memory layout in ddr2_buf (each element is int64_t = 8 bytes):
//
//   ┌─────────────────────────────────────────────────────────┐
//   │  Inner area (fixed, 48 MB)                              │
//   │  Partition p slot: [p * GRACE_INNER_SLOT_WORDS, ...)    │
//   │  Each tuple: 2 words  (key, packed_tid)                 │
//   ├─────────────────────────────────────────────────────────┤
//   │  Outer area (dynamic, starts at GRACE_OUTER_AREA_BASE)  │
//   │  Partition p slot: [OUTER_BASE + p * outer_slot_words)  │
//   │  outer_slot_words = ((outer_total/K)+1) * 2 * 2         │
//   └─────────────────────────────────────────────────────────┘
//
// Tuple encoding in DDR2:
//   word[0] = key  (int64_t; int32 keys sign-extended)
//   word[1] = (int64_t)(((uint64_t)tid.blkno << 32) | ((uint64_t)tid.offno << 16))
//
// Phase sequence (called from hash_join_kernel.cpp):
//   build_inner_partitions()  — receives INNER_DATA, writes to inner DDR2 area
//   build_outer_partitions()  — receives OUTER_DATA, writes to outer DDR2 area
//   join_all_partitions()     — for each partition: load inner into BRAM, probe outer

class GraceHashEngine {
public:

    // ── Partition hash ─────────────────────────────────────────────────────────
    // Uses Knuth multiplicative hash with 32-bit constant (different from
    // LinearHashTable's Fibonacci hash to avoid correlation between the two layers).
    static uint32_t partition_of(int64_t key, uint32_t K) {
#pragma HLS INLINE
        return (uint32_t)(((uint64_t)key * 2654435761ull) >> 32u) % K;
    }

    // ── DDR2 tuple encoding ────────────────────────────────────────────────────

    static void pack_tuple(int64_t key, Tid tid, int64_t* w) {
#pragma HLS INLINE
        w[0] = key;
        w[1] = (int64_t)(((uint64_t)tid.blkno << 32u) |
                          ((uint64_t)tid.offno  << 16u));
    }

    static void unpack_tuple(const int64_t* w, int64_t& key, Tid& tid) {
#pragma HLS INLINE
        key = w[0];
        const uint64_t packed = (uint64_t)w[1];
        tid.blkno = (uint32_t)(packed >> 32u);
        tid.offno  = (uint16_t)((packed >> 16u) & 0xFFFFu);
    }

    // ── Phase 1: partition inner tuples into DDR2 ──────────────────────────────
    //
    // Receives INNER_DATA frames from rx, partitions each tuple by hash, writes
    // into the inner DDR2 area. ACKs every frame (credit = remaining inner rows).
    // Returns ERR_NONE on success, ERR_OVERFLOW / ERR_BAD_STATE on error.
    FpgaError build_inner_partitions(ByteStream& rx, ByteStream& tx,
                                      uint32_t inner_total, KeyType keytype,
                                      uint32_t K, int64_t* ddr2_buf,
                                      uint8_t session_id = 0u) {
        // Reset partition counters (256 cycles at II=1 — negligible vs DDR2 latency)
        reset_counts:
        for (uint32_t p = 0u; p < GRACE_MAX_K; p++) {
#pragma HLS PIPELINE II=1
            inner_counts[p] = 0u;
            outer_counts[p] = 0u;
        }

        uint32_t inner_received = 0u;

        build_inner_frame_loop:
        for (uint32_t frame = 0u; frame <= inner_total / 1u + 1u; frame++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=12289
            if (inner_received >= inner_total) break;

            const uint8_t  msg_type = rx_byte(rx);
            const uint16_t count    = rx_u16(rx);

            if (msg_type == (uint8_t)MSG_RESET) {
                tx_status_frame(tx, MSG_ACK, PHASE_IDLE, 0u, 0u, 0u, session_id);
                return ERR_BAD_STATE;
            }

            if (msg_type != (uint8_t)MSG_INNER_DATA) {
                tx_error(tx, ERR_BAD_STATE);
                return ERR_BAD_STATE;
            }

            build_inner_tuple_loop:
            for (uint16_t i = 0u; i < count; i++) {
#pragma HLS PIPELINE
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                int64_t key;
                Tid     tid;
                if (keytype == KEY_INT64) {
                    key = rx_i64(rx);
                    tid = rx_tid(rx);
                } else {
                    key = (int64_t)(int32_t)rx_u32(rx);
                    tid = rx_tid(rx);
                }

                const uint32_t p = partition_of(key, K);
                if (inner_counts[p] >= HT_MAX_ROWS) {
                    for (uint16_t j = (uint16_t)(i + 1u); j < count; j++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=255
                        rx_discard_tuple(rx, keytype);
                    }
                    tx_error(tx, ERR_OVERFLOW);
                    return ERR_OVERFLOW;
                }
                const uint32_t idx = p * GRACE_INNER_SLOT_WORDS +
                                     inner_counts[p] * GRACE_WORDS_PER_TUPLE;
                pack_tuple(key, tid, &ddr2_buf[idx]);
                inner_counts[p]++;
                inner_received++;
            }

            // ACK each frame; credit = remaining inner rows (capped at advertised credit)
            const uint32_t remaining = inner_total - inner_received;
            const uint16_t credit = (remaining < RX_CREDIT_TUPLES)
                                    ? (uint16_t)remaining
                                    : (uint16_t)RX_CREDIT_TUPLES;
            tx_status_frame(tx, MSG_ACK, PHASE_BUILDING, inner_received, 0u,
                            credit, session_id);
        }
        return ERR_NONE;
    }

    // ── Phase 2: partition outer tuples into DDR2 ──────────────────────────────
    //
    // Receives OUTER_DATA frames from rx, partitions each tuple by hash, writes
    // into the outer DDR2 area. ACKs every frame. Does NOT emit results yet.
    // outer_slot_words: computed by kernel at CONFIGURE time as:
    //   ((outer_total / K) + 1) * GRACE_WORDS_PER_TUPLE * 2
    FpgaError build_outer_partitions(ByteStream& rx, ByteStream& tx,
                                      uint32_t outer_total, KeyType keytype,
                                      uint32_t K, uint32_t outer_slot_words,
                                      int64_t* ddr2_buf,
                                      uint8_t session_id = 0u) {
        uint32_t outer_received = 0u;

        build_outer_frame_loop:
        for (uint32_t frame = 0u; frame <= outer_total / 1u + 1u; frame++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=12289
            if (outer_received >= outer_total) break;

            const uint8_t  msg_type = rx_byte(rx);
            const uint16_t count    = rx_u16(rx);

            if (msg_type == (uint8_t)MSG_RESET) {
                tx_status_frame(tx, MSG_ACK, PHASE_IDLE, 0u, 0u, 0u, session_id);
                return ERR_BAD_STATE;
            }

            if (msg_type != (uint8_t)MSG_OUTER_DATA) {
                tx_error(tx, ERR_BAD_STATE);
                return ERR_BAD_STATE;
            }

            build_outer_tuple_loop:
            for (uint16_t i = 0u; i < count; i++) {
#pragma HLS PIPELINE
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                int64_t key;
                Tid     tid;
                if (keytype == KEY_INT64) {
                    key = rx_i64(rx);
                    tid = rx_tid(rx);
                } else {
                    key = (int64_t)(int32_t)rx_u32(rx);
                    tid = rx_tid(rx);
                }

                const uint32_t p = partition_of(key, K);
                const uint32_t max_per_slot = outer_slot_words / GRACE_WORDS_PER_TUPLE;
                if (outer_counts[p] >= max_per_slot) {
                    for (uint16_t j = (uint16_t)(i + 1u); j < count; j++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=255
                        rx_discard_tuple(rx, keytype);
                    }
                    tx_error(tx, ERR_OVERFLOW);
                    return ERR_OVERFLOW;
                }
                const uint32_t idx = GRACE_OUTER_AREA_BASE +
                                     p * outer_slot_words +
                                     outer_counts[p] * GRACE_WORDS_PER_TUPLE;
                pack_tuple(key, tid, &ddr2_buf[idx]);
                outer_counts[p]++;
                outer_received++;
            }

            // ACK each frame; credit = remaining outer rows (capped at advertised credit)
            const uint32_t remaining = outer_total - outer_received;
            const uint16_t credit = (remaining < RX_CREDIT_TUPLES)
                                    ? (uint16_t)remaining
                                    : (uint16_t)RX_CREDIT_TUPLES;
            tx_status_frame(tx, MSG_ACK, PHASE_PROBING, outer_received, 0u,
                            credit, session_id);
        }
        return ERR_NONE;
    }

    // ── Phase 3: for each partition, build BRAM HT from inner, probe outer ─────
    //
    // All data is already in DDR2. No AXI-Stream reads here — only writes (RESULT).
    // Returns ERR_NONE on success and writes the total number of emitted pairs.
    // A partition can be under HT_MAX_ROWS but still overflow the bounded
    // linear-probe window, so replay build errors must not be ignored.
    FpgaError join_all_partitions(ByteStream& tx,
                                  uint32_t K, uint32_t outer_slot_words,
                                  int64_t* ddr2_buf,
                                  LinearHashTable& ht,
                                  ResultPair result_buf[TX_BUF_SIZE],
                                  uint32_t& matched_out) {
        uint32_t matched = 0u;
        matched_out = 0u;

        join_partition_loop:
        for (uint32_t p = 0u; p < K; p++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256

            ht.reset();

            // Load inner partition p from DDR2 into BRAM hash table
            join_build_loop:
            for (uint32_t i = 0u; i < inner_counts[p]; i++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=12288
                int64_t key;
                Tid     inner_tid;
                const uint32_t idx = p * GRACE_INNER_SLOT_WORDS +
                                     i * GRACE_WORDS_PER_TUPLE;
                unpack_tuple(&ddr2_buf[idx], key, inner_tid);
                const FpgaError err = ht.build(key, inner_tid);
                if (err != ERR_NONE) {
                    tx_error(tx, err);
                    matched_out = matched;
                    return err;
                }
            }

            // Probe outer partition p against the BRAM hash table
            uint16_t buf_cnt = 0u;

            join_probe_loop:
            for (uint32_t j = 0u; j < outer_counts[p]; j++) {
#pragma HLS LOOP_TRIPCOUNT min=0 max=12288
                int64_t key;
                Tid     outer_tid;
                const uint32_t idx = GRACE_OUTER_AREA_BASE +
                                     p * outer_slot_words +
                                     j * GRACE_WORDS_PER_TUPLE;
                unpack_tuple(&ddr2_buf[idx], key, outer_tid);

                Tid inner_tid;
                if (ht.probe(key, inner_tid)) {
                    result_buf[buf_cnt].inner_tid = inner_tid;
                    result_buf[buf_cnt].outer_tid = outer_tid;
                    buf_cnt++;
                    matched++;
                    if (buf_cnt == (uint16_t)TX_BUF_SIZE) {
                        tx_results(tx, result_buf, buf_cnt);
                        buf_cnt = 0u;
                    }
                }
            }

            // Flush remaining results for this partition
            if (buf_cnt > 0u) {
                tx_results(tx, result_buf, buf_cnt);
            }
        }
        matched_out = matched;
        return ERR_NONE;
    }

    // Partition row counters — partitioned in build_inner_partitions via pragma.
    uint32_t inner_counts[GRACE_MAX_K];
    uint32_t outer_counts[GRACE_MAX_K];
};
