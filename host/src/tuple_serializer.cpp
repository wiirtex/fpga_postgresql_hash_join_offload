#include "tuple_serializer.hpp"
#include <algorithm>
#include <cassert>

TupleSerializer::TupleSerializer(KeyType key_type) : key_type_(key_type) {}

// ─── Private byte helpers (little-endian) ────────────────────────────────────

void TupleSerializer::put_u8(std::vector<uint8_t>& out, uint8_t v) {
    out.push_back(v);
}

void TupleSerializer::put_u16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
}

void TupleSerializer::put_u32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 24));
}

void TupleSerializer::put_i32(std::vector<uint8_t>& out, int32_t v) {
    put_u32(out, static_cast<uint32_t>(v));
}

void TupleSerializer::put_i64(std::vector<uint8_t>& out, int64_t v) {
    uint64_t u = static_cast<uint64_t>(v);
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<uint8_t>(u >> (8 * i)));
}

void TupleSerializer::put_tid(std::vector<uint8_t>& out, const Tid& tid) {
    put_u32(out, tid.blkno);
    put_u16(out, tid.offno);
}

void TupleSerializer::put_tuple(std::vector<uint8_t>& out, const InputTuple& t) {
    if (key_type_ == KEY_INT32) {
        put_i32(out, static_cast<int32_t>(t.key));
    } else {
        put_i64(out, t.key);
    }
    put_tid(out, t.tid);
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::vector<uint8_t> TupleSerializer::serialize_configure(const JoinConfig& cfg,
                                                            uint32_t          inner_count,
                                                            uint32_t          outer_count,
                                                            uint8_t           session_id) {
    std::vector<uint8_t> out;
    put_u8 (out, MSG_CONFIGURE);
    put_u16(out, 1u);                                     // count = 1 ConfigurePayload
    put_u8 (out, cfg.algorithm);
    put_u8 (out, static_cast<uint8_t>(cfg.key_type));
    put_u16(out, static_cast<uint16_t>(RX_CREDIT_TUPLES));  // advertise flow-control credit
    put_u32(out, inner_count);
    put_u32(out, outer_count);
    put_u8 (out, session_id);
    return out;
}

std::vector<uint8_t> TupleSerializer::serialize_batch(MsgType           msg_type,
                                                        const InputTuple* tuples,
                                                        size_t            count,
                                                        size_t            max_count) {
    size_t limit = std::min(count, max_count);
    if (limit == 0) return {};

    // Build payload separately so we can write the accurate count in the header.
    std::vector<uint8_t> payload;
    for (size_t i = 0; i < limit; ++i)
        put_tuple(payload, tuples[i]);

    std::vector<uint8_t> out;
    put_u8 (out, static_cast<uint8_t>(msg_type));
    put_u16(out, static_cast<uint16_t>(limit));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
