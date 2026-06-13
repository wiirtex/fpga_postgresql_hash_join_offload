#include "fpga_client.hpp"
#include "result_decoder.hpp"
#include "udp_transport.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <thread>
#include <vector>

static int64_t outer_key_for(const std::string& pattern,
                             size_t g,
                             size_t inner_n,
                             size_t outer_n) {
    if (pattern == "full")
        return static_cast<int64_t>(((g - 1u) % inner_n) + 1u);
    if (pattern == "half")
        return (g % 2u) == 0u
            ? static_cast<int64_t>(((g - 1u) % inner_n) + 1u)
            : static_cast<int64_t>(inner_n + g);
    if (pattern == "low10")
        return (g % 10u) == 0u
            ? static_cast<int64_t>(((g - 1u) % inner_n) + 1u)
            : static_cast<int64_t>(inner_n + g);
    if (pattern == "none")
        return static_cast<int64_t>(inner_n + g);
    if (pattern == "hotkey")
        return 1;
    if (pattern == "outer_skew")
        return g <= (outer_n * 9u) / 10u
            ? 1
            : static_cast<int64_t>(((g - 1u) % inner_n) + 1u);
    throw FpgaException("unknown pattern: " + pattern);
}

static size_t expected_count_for(const std::string& pattern, size_t outer_n) {
    if (pattern == "full" || pattern == "hotkey" || pattern == "outer_skew")
        return outer_n;
    if (pattern == "half")
        return outer_n / 2u;
    if (pattern == "low10")
        return outer_n / 10u;
    if (pattern == "none")
        return 0;
    throw FpgaException("unknown pattern: " + pattern);
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
    const size_t inner_n = argc >= 3 ? static_cast<size_t>(std::strtoull(argv[2], nullptr, 10)) : 16u;
    const size_t outer_n = argc >= 4 ? static_cast<size_t>(std::strtoull(argv[3], nullptr, 10)) : 64u;
    const std::string pattern = argc >= 5 ? argv[4] : "full";
    const std::string algorithm_s = argc >= 6 ? argv[5] : "a";
    const uint32_t gap_ms = argc >= 7 ? static_cast<uint32_t>(std::strtoul(argv[6], nullptr, 10)) : 20u;
    const uint16_t max_batch = argc >= 8 ? static_cast<uint16_t>(std::strtoul(argv[7], nullptr, 10)) : 64u;
    const size_t max_udp_payload = argc >= 9 ? static_cast<size_t>(std::strtoull(argv[8], nullptr, 10)) : 1200u;
    const uint16_t ack_window = argc >= 10 ? static_cast<uint16_t>(std::strtoul(argv[9], nullptr, 10)) : 1u;

    try {
        std::vector<InputTuple> inner;
        std::vector<InputTuple> outer;
        inner.reserve(inner_n);
        outer.reserve(outer_n);

        for (size_t i = 1u; i <= inner_n; ++i) {
            inner.push_back({static_cast<int64_t>(i),
                             {1u, static_cast<uint16_t>(i & 0xFFFFu)}});
        }
        for (size_t g = 1u; g <= outer_n; ++g) {
            outer.push_back({outer_key_for(pattern, g, inner_n, outer_n),
                             {2u, static_cast<uint16_t>(g & 0xFFFFu)}});
        }

        UdpTransport udp(fpga_ip, FPGA_UDP_PORT, FPGA_UDP_PORT, 100, max_udp_payload);
        prepare_udp_session(udp);

        ClientConfig ccfg;
        ccfg.warn_timeout_ms = 5000;
        ccfg.hard_timeout_ms = 30000;
        ccfg.max_batch_tuples = max_batch;
        ccfg.ack_window_frames = ack_window;
        ccfg.inter_batch_gap_ms = gap_ms;
        ccfg.coalesce_config_first_inner = true;
        ccfg.infer_done_from_probe_ack = true;
        ccfg.reset_after_run = true;

        JoinConfig cfg;
        if (algorithm_s == "a" || algorithm_s == "A" || algorithm_s == "0") {
            cfg.algorithm = static_cast<uint8_t>(ALGORITHM_A);
        } else if (algorithm_s == "b" || algorithm_s == "B" || algorithm_s == "1") {
            cfg.algorithm = static_cast<uint8_t>(ALGORITHM_B);
        } else {
            throw FpgaException("unknown algorithm: " + algorithm_s);
        }
        cfg.key_type = KEY_INT32;

        FpgaClient client(udp, ccfg);
        const auto t0 = std::chrono::steady_clock::now();
        auto results = client.run_hash_join(cfg, inner.data(), inner.size(),
                                            outer.data(), outer.size());
        const auto t1 = std::chrono::steady_clock::now();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        const auto& metrics = client.last_host_metrics();

        const size_t expected = expected_count_for(pattern, outer_n);
        std::printf("udp_case alg=%s inner=%zu outer=%zu pattern=%s gap_ms=%u max_batch=%u max_udp_payload=%zu ack_window=%u results=%zu expected=%zu time_ms=%lld\n",
                    algorithm_s.c_str(), inner_n, outer_n, pattern.c_str(), gap_ms,
                    static_cast<unsigned>(max_batch), max_udp_payload,
                    static_cast<unsigned>(ack_window), results.size(), expected,
                    static_cast<long long>(ms));
        std::printf("metrics frames_sent=%llu frames_recv=%llu sends=%llu inner_frames=%llu outer_frames=%llu ack=%llu result_frames=%llu timing=%llu adapter_ms=%.3f probe_wait_ms=%.3f result_recv_ms=%.3f\n",
                    static_cast<unsigned long long>(metrics.protocol_frames_sent),
                    static_cast<unsigned long long>(metrics.protocol_frames_recv),
                    static_cast<unsigned long long>(metrics.transport_sends),
                    static_cast<unsigned long long>(metrics.inner_frames_sent),
                    static_cast<unsigned long long>(metrics.outer_frames_sent),
                    static_cast<unsigned long long>(metrics.ack_frames_recv),
                    static_cast<unsigned long long>(metrics.result_frames_recv),
                    static_cast<unsigned long long>(metrics.timing_frames_recv),
                    metrics.adapter_total_ms,
                    metrics.probe_ack_wait_ms,
                    metrics.result_recv_ms);
        if (metrics.has_board_timing) {
            std::printf("board inner_frames=%u outer_frames=%u result_frames=%u bytes_rx=%u bytes_tx=%u total_cycles=%llu rx_wait=%llu tx_blocked=%llu protocol_wait=%llu\n",
                        metrics.board_timing.inner_frames,
                        metrics.board_timing.outer_frames,
                        metrics.board_timing.result_frames,
                        metrics.board_timing.bytes_rx,
                        metrics.board_timing.bytes_tx,
                        static_cast<unsigned long long>(metrics.board_timing.session_total_cycles),
                        static_cast<unsigned long long>(metrics.board_timing.rx_wait_cycles),
                        static_cast<unsigned long long>(metrics.board_timing.tx_blocked_cycles),
                        static_cast<unsigned long long>(metrics.board_timing.protocol_wait_cycles));
        }
        if (results.size() != expected) {
            std::fprintf(stderr, "udp_case: result count mismatch\n");
            return 1;
        }
        std::printf("udp_case PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "udp_case failed: %s\n", e.what());
        return 1;
    }
}
