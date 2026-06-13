#pragma once
#include "hash_join_types.hpp"

// ─── LinearHashTable ──────────────────────────────────────────────────────────
//
// Open-addressing hash table with linear probing. Designed to live entirely in
// Artix-7 Block RAM. No dynamic allocation, no pointers — FPGA-friendly.
//
// Key type  : int64_t (int32 keys are sign-extended before insert/lookup)
// Capacity  : HT_MAX_ROWS = 12 288 rows  (75% of 16 384 slots)
// Probe     : bounded by MAX_PROBE_DIST = 16 steps  (constant worst-case latency)
// BRAM use  : ~240 KB  ≈ 40% of Artix-7 XC7A100T total budget
//
//   ht_key[HT_SLOTS]   int64_t   128 KB
//   ht_blk[HT_SLOTS]   uint32_t   64 KB
//   ht_off[HT_SLOTS]   uint16_t   32 KB
//   ht_valid[HT_SLOTS] uint8_t    16 KB
//                                ──────
//                                240 KB
//
// Usage pattern:
//   ht.reset();                        // clear table before build phase
//   ht.build(key, tid);                // insert each inner-table row
//   ht.probe(key, out_inner_tid);      // look up each outer-table row

class LinearHashTable {
public:

    // ── Public API ────────────────────────────────────────────────────────────

    // Clear all slots. Must be called before each build phase.
    // Conservative banked candidate: split each table field into 16 cyclic
    // banks so a linear-probe window can be pipelined without relying on HLS to
    // infer a safe memory architecture from the batch loops.
    void reset() {
#pragma HLS ARRAY_PARTITION variable=ht_key cyclic factor=16 dim=1
#pragma HLS ARRAY_PARTITION variable=ht_blk cyclic factor=16 dim=1
#pragma HLS ARRAY_PARTITION variable=ht_off cyclic factor=16 dim=1
#pragma HLS ARRAY_PARTITION variable=ht_valid cyclic factor=16 dim=1
        for (uint32_t s = 0; s < HT_SLOTS; s++) {
#pragma HLS PIPELINE
            ht_valid[s] = 0;
        }
        row_count = 0;
    }

    // Insert (key, tid) into the hash table.
    //   Returns ERR_NONE      on success.
    //   Returns ERR_OVERFLOW  if table is full or the probe chain is exhausted.
    // Complexity: O(1) average; O(MAX_PROBE_DIST) worst case.
    // The probe chain is explicitly pipelined, but the caller's batch loop is
    // left sequential to avoid the earlier full-unroll hardware regression.
    FpgaError build(int64_t key, Tid tid) {
#pragma HLS INLINE
        uint32_t unused_steps = 0u;
        return build(key, tid, unused_steps);
    }

    FpgaError build(int64_t key, Tid tid, uint32_t& steps_out) {
#pragma HLS INLINE off
        steps_out = 0u;
        if (row_count >= HT_MAX_ROWS) return ERR_OVERFLOW;

        const uint32_t start = slot_of(key);

        for (uint32_t step = 0; step < MAX_PROBE_DIST; step++) {
#pragma HLS DEPENDENCE variable=ht_key inter false
#pragma HLS DEPENDENCE variable=ht_blk inter false
#pragma HLS DEPENDENCE variable=ht_off inter false
#pragma HLS DEPENDENCE variable=ht_valid inter false
#pragma HLS PIPELINE II=2
#pragma HLS LOOP_TRIPCOUNT min=1 max=16
            const uint32_t s = (start + step) & (HT_SLOTS - 1u);
            steps_out = step + 1u;
            if (!ht_valid[s]) {
                ht_key[s]   = key;
                ht_blk[s]   = tid.blkno;
                ht_off[s]   = tid.offno;
                ht_valid[s] = 1u;
                row_count++;
                return ERR_NONE;
            }
        }
        return ERR_OVERFLOW;  // probe chain exhausted — table too skewed
    }

    // Look up a key.
    //   Returns 1 and writes out_tid when key is found.
    //   Returns 0 on miss (key absent or probe chain exhausted).
    // Complexity: O(1) average; O(MAX_PROBE_DIST) worst case.
    // The probe chain is explicitly pipelined, but the caller's batch loop is
    // left sequential to avoid the earlier full-unroll hardware regression.
    int probe(int64_t key, Tid& out_tid) {
#pragma HLS INLINE
        uint32_t unused_steps = 0u;
        return probe(key, out_tid, unused_steps);
    }

    int probe(int64_t key, Tid& out_tid, uint32_t& steps_out) {
#pragma HLS INLINE off
        steps_out = 0u;
        const uint32_t start = slot_of(key);

        for (uint32_t step = 0; step < MAX_PROBE_DIST; step++) {
#pragma HLS DEPENDENCE variable=ht_key inter false
#pragma HLS DEPENDENCE variable=ht_blk inter false
#pragma HLS DEPENDENCE variable=ht_off inter false
#pragma HLS DEPENDENCE variable=ht_valid inter false
#pragma HLS PIPELINE II=2
#pragma HLS LOOP_TRIPCOUNT min=1 max=16
            const uint32_t s = (start + step) & (HT_SLOTS - 1u);
            steps_out = step + 1u;
            if (!ht_valid[s]) return 0;   // empty slot → key is definitely absent
            if (ht_key[s] == key) {
                out_tid.blkno = ht_blk[s];
                out_tid.offno = ht_off[s];
                return 1;
            }
        }
        return 0;  // not found within MAX_PROBE_DIST steps
    }

    uint32_t size()     const { return row_count; }
    uint32_t capacity() const { return HT_MAX_ROWS; }

private:

    // ── BRAM-backed storage arrays ────────────────────────────────────────────
    // Declared as separate arrays (not struct-of-arrays) so HLS can partition
    // and pipeline accesses to each field independently.
    int64_t  ht_key[HT_SLOTS];    // join keys             (128 KB)
    uint32_t ht_blk[HT_SLOTS];    // TID block numbers      (64 KB)
    uint16_t ht_off[HT_SLOTS];    // TID item offsets        (32 KB)
    uint8_t  ht_valid[HT_SLOTS];  // slot occupancy flags    (16 KB)

    uint32_t row_count;

    // Fibonacci (Knuth multiplicative) hash: int64 key → slot index.
    // Uses the golden-ratio constant 2^64 / phi ≈ 11400714819323198485.
    // Properties: uniform distribution for sequential PKs; 1 DSP multiply; 1 cycle.
    static uint32_t slot_of(int64_t key) {
#pragma HLS INLINE
        return (uint32_t)(
            ((uint64_t)key * 11400714819323198485ull) >> (64u - HT_LOG2_SLOTS)
        );
    }
};
