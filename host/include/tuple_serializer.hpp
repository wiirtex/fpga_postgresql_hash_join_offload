#pragma once
#include "fpga_types.hpp"
#include <cstddef>
#include <vector>

// Converts host-side tuples into wire-format byte frames.
//
// Responsibilities:
//   - Little-endian byte packing of all fields.
//   - Key type dispatch (int32 vs int64 tuple wire format).
//
// NOT responsible for:
//   - NULL filtering     — caller's responsibility before invoking serialize_batch.
//   - Deduplication      — PostgreSQL adapter's responsibility (Stage 3).
//   - Flow control       — FpgaClient's responsibility.
class TupleSerializer {
public:
    explicit TupleSerializer(KeyType key_type);

    // Serialize a CONFIGURE frame.
    // Wire: [MSG_CONFIGURE:1B][count=1:2B][ConfigurePayload:13B] = 16 bytes.
    // inner_count / outer_count are passed explicitly — not stored in JoinConfig.
    std::vector<uint8_t> serialize_configure(const JoinConfig& cfg,
                                              uint32_t          inner_count,
                                              uint32_t          outer_count,
                                              uint8_t           session_id);

    // Serialize one batch of tuples as MSG_INNER_DATA or MSG_OUTER_DATA.
    // Processes exactly min(count, max_count) tuples.
    // Returns empty vector if count == 0 or max_count == 0 (caller must not send).
    std::vector<uint8_t> serialize_batch(MsgType           msg_type,
                                          const InputTuple* tuples,
                                          size_t            count,
                                          size_t            max_count);

private:
    KeyType key_type_;

    static void put_u8 (std::vector<uint8_t>& out, uint8_t  v);
    static void put_u16(std::vector<uint8_t>& out, uint16_t v);
    static void put_u32(std::vector<uint8_t>& out, uint32_t v);
    static void put_i32(std::vector<uint8_t>& out, int32_t  v);
    static void put_i64(std::vector<uint8_t>& out, int64_t  v);
    static void put_tid(std::vector<uint8_t>& out, const Tid& tid);
    void        put_tuple(std::vector<uint8_t>& out, const InputTuple& t);
};
