#pragma once
// Re-exports all wire protocol types from the HLS kernel.
// hash_join_types.hpp uses only <stdint.h> — safe to include on the host.
#include "hash_join_types.hpp"

// ─── Host-side convenience types ─────────────────────────────────────────────
// Not transmitted on the wire directly; used by TupleSerializer / FpgaClient.

// A join key + TID pair as presented by the caller (e.g. PostgreSQL adapter).
// key is always stored as int64_t; TupleSerializer truncates to int32_t for KEY_INT32.
struct InputTuple {
    int64_t key;
    Tid     tid;
};

// Join configuration.
// inner_count / outer_count are intentionally absent: FpgaClient derives them
// from the actual array sizes passed to run_hash_join(), so the CONFIGURE frame
// always contains exact counts (planner estimates must not leak here).
struct JoinConfig {
    uint8_t algorithm = 0;          // 0 = LinearProbing, 1 = GraceHJ
    KeyType key_type  = KEY_INT32;
};
