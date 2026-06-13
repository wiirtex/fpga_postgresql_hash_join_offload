#pragma once
#include "hash_join_types.hpp"   // MsgType, FpgaError, Phase, Tid, ResultPair
#include "transport.hpp"
#include <vector>

// Sentinel returned by recv_frame() when transport has no data yet.
// Not a real protocol message — value 0x00 is unused in MsgType enum.
inline constexpr MsgType MSG_NONE = static_cast<MsgType>(0x00);

// Acknowledgement / status info parsed from MSG_ACK or MSG_STATUS frames.
struct AckInfo {
    uint8_t  phase          = PHASE_IDLE;
    uint8_t  session_id     = 0;
    uint16_t credit         = 0;
    uint32_t rows_processed = 0;
    uint32_t rows_matched   = 0;
};

// Parses raw bytes from ITransport into typed protocol frames.
//
// Usage:
//   ResultDecoder dec;
//   std::vector<ResultPair> pairs;
//   AckInfo ack;
//   FpgaError err;
//   MsgType msg = dec.recv_frame(transport, pairs, ack, err);
class ResultDecoder {
public:
    // Try to receive and parse one complete frame.
    //
    // Returns MSG_NONE if transport has no data (transport returned 0 on first byte).
    // Returns the actual MsgType otherwise; fills the appropriate out-parameter:
    //   MSG_RESULT  → out_pairs populated
    //   MSG_ACK     → out_ack populated
    //   MSG_STATUS  → out_ack populated
    //   MSG_ERROR   → out_error populated
    //
    // Throws std::runtime_error on unknown msg_type or transport error.
    MsgType recv_frame(ITransport&              transport,
                       std::vector<ResultPair>& out_pairs,
                       AckInfo&                 out_ack,
                       FpgaError&               out_error);

    bool has_timing_summary() const { return has_timing_; }
    const TimingSummaryPayload& timing_summary() const { return timing_; }
    uint32_t timing_frames_received() const { return timing_frames_; }

private:
    // Block until exactly n bytes are received. Throws if a partial protocol
    // frame stops making progress so the caller cannot hang forever.
    static void recv_exact(ITransport& t, void* buf, size_t n);

    static uint16_t read_u16(const uint8_t* p);
    static uint32_t read_u32(const uint8_t* p);
    static uint64_t read_u64(const uint8_t* p);
    static Tid      read_tid(const uint8_t* p);

    bool has_timing_ = false;
    uint32_t timing_frames_ = 0;
    TimingSummaryPayload timing_{};
};
