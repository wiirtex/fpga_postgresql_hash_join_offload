#include "fpga_client.hpp"
#include "udp_transport.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <thread>
#include <vector>

static bool same_pair(const ResultPair& r, const Tid& inner, const Tid& outer) {
    return r.inner_tid.blkno == inner.blkno &&
           r.inner_tid.offno == inner.offno &&
           r.outer_tid.blkno == outer.blkno &&
           r.outer_tid.offno == outer.offno;
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

    try {
        UdpTransport udp(fpga_ip, FPGA_UDP_PORT, FPGA_UDP_PORT, 500, 1200);
        prepare_udp_session(udp);

        ClientConfig ccfg;
        ccfg.warn_timeout_ms = 500;
        ccfg.hard_timeout_ms = 3000;
        ccfg.inter_batch_gap_ms = 20;
        ccfg.coalesce_config_first_inner = true;
        ccfg.infer_done_from_probe_ack = true;
        ccfg.reset_after_run = true;

        FpgaClient client(udp, ccfg);

        JoinConfig cfg;
        cfg.algorithm = 0;
        cfg.key_type = KEY_INT32;

        InputTuple inner[] = {
            {10, {1, 1}},
            {20, {1, 2}},
            {30, {1, 3}},
        };
        InputTuple outer[] = {
            {99, {2, 1}},
            {20, {2, 2}},
            {10, {2, 3}},
        };

        auto results = client.run_hash_join(cfg, inner, 3, outer, 3);
        std::printf("udp_smoke results=%zu\n", results.size());
        for (const auto& r : results) {
            std::printf("  inner=(%u,%u) outer=(%u,%u)\n",
                        r.inner_tid.blkno, r.inner_tid.offno,
                        r.outer_tid.blkno, r.outer_tid.offno);
        }

        const bool ok =
            results.size() == 2 &&
            std::any_of(results.begin(), results.end(), [](const ResultPair& r) {
                return same_pair(r, {1, 2}, {2, 2});
            }) &&
            std::any_of(results.begin(), results.end(), [](const ResultPair& r) {
                return same_pair(r, {1, 1}, {2, 3});
            });

        if (!ok) {
            std::fprintf(stderr, "udp_smoke: unexpected join results\n");
            return 1;
        }
        std::printf("udp_smoke PASS\n");
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "udp_smoke failed: %s\n", e.what());
        return 1;
    }
}
