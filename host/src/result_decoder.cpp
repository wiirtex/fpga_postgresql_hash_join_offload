#include "result_decoder.hpp"
#include "debug_codes.hpp"
#include "logger.hpp"
#include <stdexcept>
#include <string>

void ResultDecoder::recv_exact(ITransport& t, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        size_t got = t.recv(p, n);
        if (got > 0) {
            p += got;
            n -= got;
        } else {
            throw std::runtime_error("ResultDecoder: partial protocol frame timed out");
        }
    }
}

uint16_t ResultDecoder::read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t ResultDecoder::read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t ResultDecoder::read_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (unsigned i = 0; i < 8; ++i)
        v |= static_cast<uint64_t>(p[i]) << (8u * i);
    return v;
}

Tid ResultDecoder::read_tid(const uint8_t* p) {
    return { read_u32(p), read_u16(p + 4) };
}

static void require_count(uint16_t got, uint16_t expected, const char* msg_name) {
    if (got != expected) {
        throw std::runtime_error(std::string("ResultDecoder: bad ") + msg_name +
                                 " count " + std::to_string(got));
    }
}

MsgType ResultDecoder::recv_frame(ITransport&              transport,
                                   std::vector<ResultPair>& out_pairs,
                                   AckInfo&                 out_ack,
                                   FpgaError&               out_error) {
    // Try to read first byte non-blockingly.
    // If transport returns 0, report "no data" so callers can run watchdog.
    uint8_t msg_type_byte = 0;
    if (transport.recv(&msg_type_byte, 1) == 0)
        return MSG_NONE;

    // Got the first byte; read the 2-byte count field.
    uint8_t count_bytes[2];
    recv_exact(transport, count_bytes, 2);
    uint16_t count = read_u16(count_bytes);

    auto msg = static_cast<MsgType>(msg_type_byte);

    switch (msg) {

    case MSG_RESULT: {
        // Payload: count * 12-byte ResultPair.
        if (count > TX_BUF_SIZE) {
            throw std::runtime_error("ResultDecoder: oversized MSG_RESULT count " +
                                     std::to_string(count));
        }
        out_pairs.clear();
        out_pairs.reserve(count);
        std::vector<uint8_t> payload(static_cast<size_t>(count) * 12u);
        if (!payload.empty())
            recv_exact(transport, payload.data(), payload.size());
        for (uint16_t i = 0; i < count; ++i) {
            const uint8_t* buf = payload.data() + static_cast<size_t>(i) * 12u;
            ResultPair rp;
            rp.inner_tid = read_tid(buf + 0);
            rp.outer_tid = read_tid(buf + 6);
            out_pairs.push_back(rp);
        }
        LOG_DEBUG("ResultDecoder", "MSG_RESULT count=%u", count);
        break;
    }

    case MSG_ACK:
    case MSG_STATUS: {
        // Payload: one 12-byte StatusPayload; count is expected to be 1.
        require_count(count, 1u, msg == MSG_ACK ? "MSG_ACK" : "MSG_STATUS");
        uint8_t buf[12];
        recv_exact(transport, buf, 12);
        out_ack.phase          = buf[0];
        out_ack.session_id     = buf[1];
        out_ack.credit         = read_u16(buf + 2);
        out_ack.rows_processed = read_u32(buf + 4);
        out_ack.rows_matched   = read_u32(buf + 8);
        LOG_DEBUG("ResultDecoder", "%s phase=%u session=%u credit=%u rows=%u matched=%u",
                  msg == MSG_ACK ? "MSG_ACK" : "MSG_STATUS",
                  out_ack.phase, out_ack.session_id, out_ack.credit,
                  out_ack.rows_processed, out_ack.rows_matched);
        break;
    }

    case MSG_ERROR: {
        // Payload: one 1-byte FpgaError; count is expected to be 1.
        require_count(count, 1u, "MSG_ERROR");
        uint8_t buf[1];
        recv_exact(transport, buf, 1);
        out_error = static_cast<FpgaError>(buf[0]);
        LOG_DEBUG("ResultDecoder", "MSG_ERROR code=0x%02X", buf[0]);
        break;
    }

    case MSG_DEBUG: {
        // Payload: one 7-byte debug record: [level:1][code:2 LE][value:4 LE].
        require_count(count, 1u, "MSG_DEBUG");
        uint8_t buf[7];
        recv_exact(transport, buf, 7);
        auto   level = static_cast<DbgLevel>(buf[0]);
        auto   code  = static_cast<DbgCode>(static_cast<uint16_t>(buf[1]) |
                                             (static_cast<uint16_t>(buf[2]) << 8));
        uint32_t value = static_cast<uint32_t>(buf[3])
                       | (static_cast<uint32_t>(buf[4]) << 8)
                       | (static_cast<uint32_t>(buf[5]) << 16)
                       | (static_cast<uint32_t>(buf[6]) << 24);

        LogLevel host_lvl = LogLevel::INFO;
        switch (level) {
        case DbgLevel::DEBUG: host_lvl = LogLevel::DEBUG; break;
        case DbgLevel::WARN:  host_lvl = LogLevel::WARN;  break;
        case DbgLevel::ERROR: host_lvl = LogLevel::ERROR; break;
        default:              host_lvl = LogLevel::INFO;  break;
        }
        Logger::instance().log(host_lvl, "[FPGA]", "%s value=%u (0x%08X)",
                               dbg_code_name(code), value, value);
        break;
    }

    case MSG_TIMING: {
        require_count(count, TIMING_SUMMARY_COUNT, "MSG_TIMING");
        uint8_t buf[TIMING_SUMMARY_BYTES];
        recv_exact(transport, buf, sizeof(buf));

        const uint8_t* p = buf;
        timing_.version = read_u16(p); p += 2;
        timing_.flags = read_u16(p); p += 2;
        timing_.clock_hz = read_u32(p); p += 4;

        timing_.inner_rows = read_u32(p); p += 4;
        timing_.outer_rows = read_u32(p); p += 4;
        timing_.matched_rows = read_u32(p); p += 4;

        timing_.inner_frames = read_u32(p); p += 4;
        timing_.outer_frames = read_u32(p); p += 4;
        timing_.result_frames = read_u32(p); p += 4;
        timing_.ack_frames = read_u32(p); p += 4;
        timing_.debug_frames = read_u32(p); p += 4;

        timing_.bytes_rx = read_u32(p); p += 4;
        timing_.bytes_tx = read_u32(p); p += 4;

        timing_.session_total_cycles = read_u64(p); p += 8;
        timing_.config_cycles = read_u64(p); p += 8;
        timing_.build_rx_cycles = read_u64(p); p += 8;
        timing_.build_compute_cycles = read_u64(p); p += 8;
        timing_.build_total_cycles = read_u64(p); p += 8;
        timing_.probe_rx_cycles = read_u64(p); p += 8;
        timing_.probe_compute_cycles = read_u64(p); p += 8;
        timing_.result_emit_cycles = read_u64(p); p += 8;
        timing_.probe_total_cycles = read_u64(p); p += 8;
        timing_.ack_emit_cycles = read_u64(p); p += 8;
        timing_.rx_wait_cycles = read_u64(p); p += 8;
        timing_.tx_blocked_cycles = read_u64(p); p += 8;
        timing_.protocol_wait_cycles = read_u64(p); p += 8;

        timing_.max_build_batch_cycles = read_u32(p); p += 4;
        timing_.max_probe_batch_cycles = read_u32(p); p += 4;
        timing_.max_result_frame_cycles = read_u32(p); p += 4;

        timing_.hash_build_inserts = read_u32(p); p += 4;
        timing_.hash_probe_lookups = read_u32(p); p += 4;
        timing_.hash_probe_hits = read_u32(p); p += 4;
        timing_.hash_probe_misses = read_u32(p); p += 4;
        timing_.hash_overflow_errors = read_u32(p); p += 4;
        timing_.hash_build_collision_steps = read_u32(p); p += 4;
        timing_.hash_probe_collision_steps = read_u32(p); p += 4;
        timing_.hash_max_build_probe_distance = read_u16(p); p += 2;
        timing_.hash_max_probe_distance = read_u16(p); p += 2;
        timing_.hash_table_load_factor_ppm = read_u32(p); p += 4;

        has_timing_ = true;
        ++timing_frames_;
        LOG_DEBUG("ResultDecoder",
                  "MSG_TIMING version=%u clock=%u total_cycles=%llu rows=%u/%u matched=%u",
                  timing_.version, timing_.clock_hz,
                  static_cast<unsigned long long>(timing_.session_total_cycles),
                  timing_.inner_rows, timing_.outer_rows, timing_.matched_rows);
        break;
    }

    default:
        throw std::runtime_error(
            "ResultDecoder: unknown msg_type 0x" + std::to_string(msg_type_byte));
    }

    return msg;
}
