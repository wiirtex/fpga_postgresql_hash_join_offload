#include "fpga_client.hpp"
#include "result_decoder.hpp"
#include "tuple_serializer.hpp"
#include "udp_transport.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

static int64_t outer_key_for(const std::string& pattern,
                             size_t i,
                             size_t inner_n) {
    if (pattern == "full")
        return static_cast<int64_t>(((i - 1u) % inner_n) + 1u);
    if (pattern == "half")
        return (i % 2u) == 0u
            ? static_cast<int64_t>(((i - 1u) % inner_n) + 1u)
            : static_cast<int64_t>(inner_n + i);
    if (pattern == "low10")
        return (i % 10u) == 0u
            ? static_cast<int64_t>(((i - 1u) % inner_n) + 1u)
            : static_cast<int64_t>(inner_n + i);
    if (pattern == "none")
        return static_cast<int64_t>(inner_n + i);
    throw FpgaException("unknown pattern: " + pattern);
}

static size_t expected_count_for(const std::string& pattern, size_t outer_n) {
    if (pattern == "full")
        return outer_n;
    if (pattern == "half")
        return outer_n / 2u;
    if (pattern == "low10")
        return outer_n / 10u;
    if (pattern == "none")
        return 0u;
    throw FpgaException("unknown pattern: " + pattern);
}

static void append_frame(std::vector<uint8_t>& dst, const std::vector<uint8_t>& src) {
    dst.insert(dst.end(), src.begin(), src.end());
}

static void prepare_udp_session(UdpTransport& udp) {
    const uint32_t saved_timeout = udp.recv_timeout_ms();
    udp.set_recv_timeout_ms(5);
    udp.reset();
    udp.drain_until_quiet(1);

    const uint8_t reset_frame[3] = { MSG_RESET, 0x00, 0x00 };
    ResultDecoder decoder;
    bool got_idle = false;

    for (unsigned attempt = 0; attempt < 2 && !got_idle; ++attempt) {
        udp.send(reset_frame, sizeof(reset_frame));
        const auto start = std::chrono::steady_clock::now();
        while (!got_idle) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= 6500)
                break;

            std::vector<ResultPair> pairs;
            AckInfo ack;
            FpgaError err = ERR_NONE;
            try {
                const MsgType msg = decoder.recv_frame(udp, pairs, ack, err);
                if (msg == MSG_ACK && ack.phase == PHASE_IDLE)
                    got_idle = true;
            } catch (const std::exception&) {
                udp.reset();
            }
        }
        if (!got_idle)
            udp.reset();
    }

    udp.drain_until_quiet(1);
    udp.set_recv_timeout_ms(saved_timeout);
}

int main(int argc, char** argv) {
    const char* fpga_ip = argc >= 2 ? argv[1] : "169.254.242.60";
    const size_t inner_n = argc >= 3 ? static_cast<size_t>(std::strtoull(argv[2], nullptr, 10)) : 4u;
    const size_t outer_n = argc >= 4 ? static_cast<size_t>(std::strtoull(argv[3], nullptr, 10)) : inner_n;
    const std::string pattern = argc >= 5 ? argv[4] : "full";
    const std::string mode = argc >= 6 ? argv[5] : "burst";
    const size_t max_batch_arg = argc >= 7 ? static_cast<size_t>(std::strtoull(argv[6], nullptr, 10)) : 118u;
    const size_t max_batch = max_batch_arg == 0u ? 1u : max_batch_arg;
    const size_t window_frames = argc >= 8 ? static_cast<size_t>(std::strtoull(argv[7], nullptr, 10)) : 4u;

    try {
        const size_t expected = expected_count_for(pattern, outer_n);
        std::vector<InputTuple> inner;
        std::vector<InputTuple> outer;
        inner.reserve(inner_n);
        outer.reserve(outer_n);
        for (size_t i = 1u; i <= inner_n; ++i) {
            inner.push_back({static_cast<int64_t>(i),
                             {1u, static_cast<uint16_t>(i & 0xffffu)}});
        }
        for (size_t i = 1u; i <= outer_n; ++i) {
            outer.push_back({outer_key_for(pattern, i, inner_n),
                             {2u, static_cast<uint16_t>(i & 0xffffu)}});
        }

        UdpTransport udp(fpga_ip, FPGA_UDP_PORT, FPGA_UDP_PORT, 100, 1472);
        prepare_udp_session(udp);

        JoinConfig cfg;
        cfg.algorithm = static_cast<uint8_t>(ALGORITHM_A);
        cfg.key_type = KEY_INT32;
        TupleSerializer ser(cfg.key_type);

        std::vector<uint8_t> burst;
        std::vector<std::vector<uint8_t>> outer_frames;
        const size_t outer_batch_limit = (mode == "windowed") ? max_batch : outer.size();
        for (size_t offset = 0u; offset < outer.size(); offset += outer_batch_limit) {
            outer_frames.push_back(ser.serialize_batch(MSG_OUTER_DATA,
                                                       outer.data() + offset,
                                                       outer.size() - offset,
                                                       outer_batch_limit));
        }
        append_frame(burst, ser.serialize_configure(cfg,
                                                     static_cast<uint32_t>(inner.size()),
                                                     static_cast<uint32_t>(outer.size()),
                                                     1u));
        append_frame(burst, ser.serialize_batch(MSG_INNER_DATA, inner.data(), inner.size(), inner.size()));
        if (mode == "burst") {
            for (const auto& frame : outer_frames)
                append_frame(burst, frame);
        } else if (mode != "split_outer" && mode != "windowed") {
            throw FpgaException("unknown mode: " + mode);
        }

        ResultDecoder decoder;
        std::vector<ResultPair> results;
        const auto start = std::chrono::steady_clock::now();
        bool done = false;
        uint32_t final_matched = 0;
        bool outer_sent = (mode == "burst");
        size_t outer_frame_next = outer_sent ? outer_frames.size() : 0u;
        size_t outer_frame_inflight = outer_sent ? outer_frames.size() : 0u;

        auto send_outer_window = [&]() {
            while (outer_frame_next < outer_frames.size() &&
                   (mode != "windowed" || outer_frame_inflight < window_frames)) {
                udp.send(outer_frames[outer_frame_next].data(),
                         outer_frames[outer_frame_next].size());
                outer_frame_next++;
                outer_frame_inflight++;
                if (mode != "windowed")
                    break;
            }
            outer_sent = (outer_frame_next >= outer_frames.size());
        };

        udp.send(burst.data(), burst.size());

        while (!done) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= 30000)
                throw FpgaTimeoutException();

            std::vector<ResultPair> pairs;
            AckInfo ack;
            FpgaError err = ERR_NONE;
            const MsgType msg = decoder.recv_frame(udp, pairs, ack, err);
            if (msg == MSG_NONE)
                continue;
            if (msg == MSG_RESULT) {
                std::printf("  RX RESULT count=%zu\n", pairs.size());
                results.insert(results.end(), pairs.begin(), pairs.end());
            } else if (msg == MSG_ERROR) {
                throw FpgaErrorException(err);
            } else if (msg == MSG_ACK || msg == MSG_STATUS) {
                std::printf("  RX %s phase=%u credit=%u rows=%u matched=%u\n",
                            msg == MSG_ACK ? "ACK" : "STATUS",
                            ack.phase, ack.credit, ack.rows_processed, ack.rows_matched);
                if (!outer_sent && ack.phase == PHASE_BUILDING && ack.rows_processed >= inner_n) {
                    send_outer_window();
                    continue;
                }
                if (mode == "windowed" && msg == MSG_ACK && ack.phase == PHASE_PROBING) {
                    if (outer_frame_inflight > 0u)
                        outer_frame_inflight--;
                    send_outer_window();
                }
                if (msg == MSG_STATUS && ack.phase == PHASE_DONE) {
                    done = true;
                    final_matched = ack.rows_matched;
                } else if ((msg == MSG_STATUS && ack.phase == PHASE_DONE) ||
                           (ack.phase == PHASE_PROBING &&
                           ack.rows_processed >= outer_n &&
                           results.size() >= expected &&
                           ack.rows_matched == expected)) {
                    final_matched = ack.rows_matched;
                    const uint8_t reset_frame[3] = { MSG_RESET, 0x00, 0x00 };
                    udp.send(reset_frame, sizeof(reset_frame));
                    done = true;
                }
            }
        }

        std::printf("udp_burst_case inner=%zu outer=%zu pattern=%s mode=%s max_batch=%zu window=%zu burst_bytes=%zu results=%zu matched=%u expected=%zu\n",
                    inner_n, outer_n, pattern.c_str(), mode.c_str(), max_batch,
                    window_frames, burst.size(), results.size(), final_matched, expected);
        if (results.size() != expected || final_matched != expected) {
            std::fprintf(stderr, "udp_burst_case: result count mismatch\n");
            return 1;
        }
        std::printf("udp_burst_case PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "udp_burst_case failed: %s\n", e.what());
        return 1;
    }
}
