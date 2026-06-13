/*
 * fpga_adapter.cpp — C++17 bridge between the PostgreSQL extension and the
 * host-side FpgaClient library.
 *
 * This file MUST NOT include any PostgreSQL headers.  It uses only:
 *   - Standard C++ / C headers
 *   - fpga_adapter.h  (pure-C struct definitions)
 *   - host library headers (fpga_client.hpp, software_kernel.hpp, ...)
 *
 * Compilation (via pg/Makefile):
 *   g++ -std=c++17 -Wall -Wextra -fPIC -I../pg/include \
 *       -I../host/include -I../src -c fpga_adapter.cpp -o fpga_adapter.o
 */

#include "fpga_adapter.h"

#include "fpga_client.hpp"       /* FpgaClient, ClientConfig, JoinConfig   */
#include "fpga_types.hpp"        /* InputTuple, ResultPair, Tid, KeyType    */
#include "result_decoder.hpp"    /* ResultDecoder for UDP reset handshake   */
#include "software_kernel.hpp"   /* SoftwareKernel : ITransport             */
#include "uart_transport.hpp"    /* UartTransport : ITransport              */
#include "udp_transport.hpp"     /* UdpTransport : ITransport               */

#include <chrono>
#include <cctype>
#include <cstdio>                /* fprintf */
#include <cstdlib>               /* malloc, free */
#include <cstring>               /* memcpy */
#include <memory>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#ifdef FPGA_DEBUG
#define FPGA_TRACE(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define FPGA_TRACE(fmt, ...) ((void) 0)
#endif

static constexpr uint32_t kHtMaxRows = 12288u;
static constexpr uint32_t kGraceMaxK = 256u;
static constexpr uint32_t kGraceWordsPerTuple = 2u;
static constexpr uint32_t kGraceInnerSlotWords = kHtMaxRows * kGraceWordsPerTuple;
static constexpr uint32_t kGraceOuterAreaBase = kGraceMaxK * kGraceInnerSlotWords;
static constexpr uint32_t kGraceDdr2TotalWords = 16u * 1024u * 1024u;
static constexpr uint32_t kGraceMaxInnerRows = kGraceMaxK * kHtMaxRows;

/* ── Layout verification ─────────────────────────────────────────────────── */
/*
 * These static_asserts catch any accidental divergence between the pure-C
 * AdapterXxx types (in fpga_adapter.h) and the C++ types used by FpgaClient
 * (in fpga_types.hpp / hash_join_types.hpp).  Failure = compile error.
 */

static_assert(sizeof(AdapterTid)        == sizeof(Tid),
              "AdapterTid vs Tid size mismatch");
static_assert(sizeof(AdapterInputTuple) == sizeof(InputTuple),
              "AdapterInputTuple vs InputTuple size mismatch");
static_assert(sizeof(AdapterResultPair) == sizeof(ResultPair),
              "AdapterResultPair vs ResultPair size mismatch");

static_assert(offsetof(AdapterTid, blkno) == offsetof(Tid, blkno),
              "Tid.blkno offset mismatch");
static_assert(offsetof(AdapterTid, offno) == offsetof(Tid, offno),
              "Tid.offno offset mismatch");

static_assert(offsetof(AdapterInputTuple, key) == offsetof(InputTuple, key),
              "InputTuple.key offset mismatch");
static_assert(offsetof(AdapterInputTuple, tid) == offsetof(InputTuple, tid),
              "InputTuple.tid offset mismatch");

static_assert(offsetof(AdapterResultPair, inner_tid) ==
                  offsetof(ResultPair, inner_tid),
              "ResultPair.inner_tid offset mismatch");
static_assert(offsetof(AdapterResultPair, outer_tid) ==
                  offsetof(ResultPair, outer_tid),
              "ResultPair.outer_tid offset mismatch");

/* ── Module-level state ──────────────────────────────────────────────────── */

static std::string g_last_error;
static AdapterHostMetrics g_last_metrics;
static bool g_have_last_metrics = false;

static void copy_host_metrics(const HostSessionMetrics& src,
                              AdapterHostMetrics& dst)
{
    dst.config_send_ms = src.config_send_ms;
    dst.config_ack_wait_ms = src.config_ack_wait_ms;
    dst.build_send_ms = src.build_send_ms;
    dst.build_ack_wait_ms = src.build_ack_wait_ms;
    dst.probe_send_ms = src.probe_send_ms;
    dst.probe_ack_wait_ms = src.probe_ack_wait_ms;
    dst.final_status_wait_ms = src.final_status_wait_ms;
    dst.reset_wait_ms = src.reset_wait_ms;
    dst.result_recv_ms = src.result_recv_ms;
    dst.adapter_total_ms = src.adapter_total_ms;

    dst.protocol_frames_sent = src.protocol_frames_sent;
    dst.protocol_frames_recv = src.protocol_frames_recv;
    dst.transport_sends = src.transport_sends;
    dst.bytes_sent = src.bytes_sent;
    dst.bytes_recv = src.bytes_recv;

    dst.config_frames_sent = src.config_frames_sent;
    dst.inner_frames_sent = src.inner_frames_sent;
    dst.outer_frames_sent = src.outer_frames_sent;
    dst.reset_frames_sent = src.reset_frames_sent;
    dst.ack_frames_recv = src.ack_frames_recv;
    dst.status_frames_recv = src.status_frames_recv;
    dst.result_frames_recv = src.result_frames_recv;
    dst.debug_frames_recv = src.debug_frames_recv;
    dst.timing_frames_recv = src.timing_frames_recv;
    dst.error_frames_recv = src.error_frames_recv;
    dst.result_pairs_recv = src.result_pairs_recv;

    dst.has_board_timing = src.has_board_timing;
    if (src.has_board_timing) {
        const auto& t = src.board_timing;
        dst.board_timing_version = t.version;
        dst.board_timing_flags = t.flags;
        dst.board_clock_hz = t.clock_hz;
        dst.board_inner_rows = t.inner_rows;
        dst.board_outer_rows = t.outer_rows;
        dst.board_matched_rows = t.matched_rows;
        dst.board_inner_frames = t.inner_frames;
        dst.board_outer_frames = t.outer_frames;
        dst.board_result_frames = t.result_frames;
        dst.board_ack_frames = t.ack_frames;
        dst.board_debug_frames = t.debug_frames;
        dst.board_bytes_rx = t.bytes_rx;
        dst.board_bytes_tx = t.bytes_tx;
        dst.board_session_total_cycles = t.session_total_cycles;
        dst.board_config_cycles = t.config_cycles;
        dst.board_build_rx_cycles = t.build_rx_cycles;
        dst.board_build_compute_cycles = t.build_compute_cycles;
        dst.board_build_total_cycles = t.build_total_cycles;
        dst.board_probe_rx_cycles = t.probe_rx_cycles;
        dst.board_probe_compute_cycles = t.probe_compute_cycles;
        dst.board_result_emit_cycles = t.result_emit_cycles;
        dst.board_probe_total_cycles = t.probe_total_cycles;
        dst.board_ack_emit_cycles = t.ack_emit_cycles;
        dst.board_rx_wait_cycles = t.rx_wait_cycles;
        dst.board_tx_blocked_cycles = t.tx_blocked_cycles;
        dst.board_protocol_wait_cycles = t.protocol_wait_cycles;
        dst.board_max_build_batch_cycles = t.max_build_batch_cycles;
        dst.board_max_probe_batch_cycles = t.max_probe_batch_cycles;
        dst.board_max_result_frame_cycles = t.max_result_frame_cycles;
        dst.board_hash_build_inserts = t.hash_build_inserts;
        dst.board_hash_probe_lookups = t.hash_probe_lookups;
        dst.board_hash_probe_hits = t.hash_probe_hits;
        dst.board_hash_probe_misses = t.hash_probe_misses;
        dst.board_hash_overflow_errors = t.hash_overflow_errors;
        dst.board_hash_build_collision_steps = t.hash_build_collision_steps;
        dst.board_hash_probe_collision_steps = t.hash_probe_collision_steps;
        dst.board_hash_max_build_probe_distance = t.hash_max_build_probe_distance;
        dst.board_hash_max_probe_distance = t.hash_max_probe_distance;
        dst.board_hash_table_load_factor_ppm = t.hash_table_load_factor_ppm;
    }
}

static bool str_eq(const char *a, const char *b)
{
    return a != nullptr && std::strcmp(a, b) == 0;
}

struct UdpEndpoint
{
    std::string host;
    uint16_t    port = FPGA_UDP_PORT;
};

static UdpEndpoint parse_udp_endpoint(const char *device)
{
    UdpEndpoint endpoint;
    endpoint.host = device ? device : "";

    const std::string::size_type colon = endpoint.host.rfind(':');
    if (colon == std::string::npos)
        return endpoint;

    const std::string port_text = endpoint.host.substr(colon + 1);
    if (port_text.empty())
        return endpoint;

    for (char c : port_text) {
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return endpoint;
    }

    const unsigned long port = std::strtoul(port_text.c_str(), nullptr, 10);
    if (port == 0 || port > 65535)
        throw FpgaException("invalid UDP port in fpga.device");

    endpoint.host = endpoint.host.substr(0, colon);
    endpoint.port = static_cast<uint16_t>(port);
    if (endpoint.host.empty())
        throw FpgaException("invalid UDP host in fpga.device");
    return endpoint;
}

static uint8_t parse_algorithm(const char *algorithm_name)
{
    const char *name = (algorithm_name && algorithm_name[0] != '\0')
                       ? algorithm_name
                       : "a";
    if (str_eq(name, "a") || str_eq(name, "A") ||
        str_eq(name, "algorithm_a") || str_eq(name, "linear"))
        return static_cast<uint8_t>(ALGORITHM_A);
    if (str_eq(name, "b") || str_eq(name, "B") ||
        str_eq(name, "algorithm_b") || str_eq(name, "grace"))
        return static_cast<uint8_t>(ALGORITHM_B);
    throw FpgaException(std::string("unsupported fpga.algorithm: ") + name);
}

static bool grace_ddr_layout_fits(size_t inner_count, size_t outer_count)
{
    if (inner_count > kGraceMaxInnerRows)
        return false;

    size_t k = (inner_count + kHtMaxRows - 1u) / kHtMaxRows;
    if (k == 0)
        k = 1;
    if (k > kGraceMaxK)
        return false;

    const uint64_t outer_slot_words =
        ((static_cast<uint64_t>(outer_count) / k) + 1u) *
        kGraceWordsPerTuple * 2u;
    return kGraceOuterAreaBase +
           static_cast<uint64_t>(k) * outer_slot_words <= kGraceDdr2TotalWords;
}

class ScopedUdpTimeout
{
public:
    ScopedUdpTimeout(UdpTransport& udp, uint32_t timeout_ms)
        : udp_(udp), saved_(udp.recv_timeout_ms())
    {
        udp_.set_recv_timeout_ms(timeout_ms);
    }

    ~ScopedUdpTimeout()
    {
        udp_.set_recv_timeout_ms(saved_);
    }

private:
    UdpTransport& udp_;
    uint32_t saved_;
};

static void prepare_udp_session(UdpTransport& udp)
{
    ScopedUdpTimeout fast_timeout(udp, 5);
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
}

/* ── Public C API ─────────────────────────────────────────────────────────── */

extern "C" {

bool fpga_adapter_run(AdapterKeyType           key_type,
                      bool                     use_simulation,
                      const char              *algorithm_name,
                      const char              *transport_name,
                      const char              *device,
                      uint32_t                 device_baud,
                      uint32_t                 warn_ms,
                      uint32_t                 hard_ms,
                      uint16_t                 max_batch_tuples,
                      uint16_t                 ack_window_frames,
                      const AdapterInputTuple *inner,
                      size_t                   inner_count,
                      const AdapterInputTuple *outer,
                      size_t                   outer_count,
                      AdapterResultPair      **out_pairs,
                      size_t                  *out_count)
{
    *out_pairs = nullptr;
    *out_count = 0;
    g_have_last_metrics = false;
    g_last_metrics = AdapterHostMetrics{};

    /* ── 1. Deduplicate inner keys ─────────────────────────────────────────
     * The FPGA linear-probing hash table raises ERR_DUPLICATE_KEY for
     * duplicate inner keys.  Remove duplicates here (last-write-wins is
     * NOT safe; we keep the FIRST occurrence and discard the rest).       */
    std::vector<InputTuple> dedup_inner;
    dedup_inner.reserve(inner_count);
    {
        std::unordered_set<int64_t> seen;
        seen.reserve(inner_count);
        const auto *in = reinterpret_cast<const InputTuple *>(inner);
        for (size_t i = 0; i < inner_count; ++i) {
            if (seen.insert(in[i].key).second)   /* true = inserted (new) */
                dedup_inner.push_back(in[i]);
        }
    }

    /* ── 2. Cast outer array (no copy needed — layout verified above) ──── */
    const auto *outer_t = reinterpret_cast<const InputTuple *>(outer);

    /* ── 3. Build JoinConfig ──────────────────────────────────────────────  */
    JoinConfig cfg;
    cfg.algorithm = parse_algorithm(algorithm_name);
    cfg.key_type  = static_cast<KeyType>(key_type);

    if (cfg.algorithm == static_cast<uint8_t>(ALGORITHM_A)) {
        if (dedup_inner.size() > kHtMaxRows) {
            g_last_error = "Algorithm A inner side exceeds BRAM hash-table capacity";
            return false;
        }
    } else if (!grace_ddr_layout_fits(dedup_inner.size(), outer_count)) {
        g_last_error = "Algorithm B input sizes exceed Nexys A7 DDR2 layout capacity";
        return false;
    }

    ClientConfig ccfg;
    ccfg.warn_timeout_ms = warn_ms;
    ccfg.hard_timeout_ms = hard_ms;
    ccfg.max_batch_tuples = max_batch_tuples;
    ccfg.ack_window_frames = ack_window_frames;

    /* ── 4. Create transport ──────────────────────────────────────────────
     * SoftwareKernel backs simulation; UartTransport backs the board path. */
    /* ── 5. Run the join ──────────────────────────────────────────────────  */
    FPGA_TRACE("[fpga_adapter] inner_count=%zu dedup=%zu outer=%zu key_type=%d\n",
            inner_count, dedup_inner.size(), outer_count, (int)key_type);
    for (size_t i = 0; i < dedup_inner.size(); ++i)
        FPGA_TRACE("[fpga_adapter]   inner[%zu] key=%lld blk=%u off=%u\n",
                i, (long long)dedup_inner[i].key,
                dedup_inner[i].tid.blkno, dedup_inner[i].tid.offno);
    for (size_t i = 0; i < outer_count; ++i)
        FPGA_TRACE("[fpga_adapter]   outer[%zu] key=%lld blk=%u off=%u\n",
                i, (long long)outer_t[i].key,
                outer_t[i].tid.blkno, outer_t[i].tid.offno);
    try {
        SoftwareKernel sim_transport;
        std::unique_ptr<UartTransport> uart_transport;
        std::unique_ptr<UdpTransport> udp_transport;
        ITransport *transport = &sim_transport;

        if (!use_simulation) {
            if (device == nullptr || device[0] == '\0') {
                throw FpgaException("fpga.device is empty");
            }

            const char *name = (transport_name && transport_name[0] != '\0')
                               ? transport_name
                               : "uart";
            if (str_eq(name, "udp")) {
                const UdpEndpoint endpoint = parse_udp_endpoint(device);
                udp_transport = std::make_unique<UdpTransport>(
                    endpoint.host, endpoint.port, FPGA_UDP_PORT, 100, 1200);
                // Keep coalesced CONFIGURE+INNER within the current 1200-byte
                // UDP payload limit. The SQL-facing GUC caps this at 118 for
                // KEY_INT32: 15 + 3 + 118 * 10 = 1198 bytes.
                ccfg.coalesce_config_first_inner = true;
                ccfg.infer_done_from_probe_ack = true;
                ccfg.reset_after_run = false;
                prepare_udp_session(*udp_transport);
                transport = udp_transport.get();
            } else if (str_eq(name, "uart")) {
                uart_transport = std::make_unique<UartTransport>(device, device_baud);
                transport = uart_transport.get();
            } else {
                throw FpgaException(std::string("unsupported fpga.transport: ") + name);
            }
        }

        FpgaClient client(*transport, ccfg);
        auto results = client.run_hash_join(cfg,
                                            dedup_inner.data(),
                                            dedup_inner.size(),
                                            outer_t,
                                            outer_count);
        copy_host_metrics(client.last_host_metrics(), g_last_metrics);
        g_have_last_metrics = true;
        FPGA_TRACE("[fpga_adapter] results=%zu\n", results.size());

        /* ── 6. Return results via malloc'd array ─────────────────────────
         * Caller (fpga_executor.c) must free() this after copying to
         * palloc'd PG memory.                                             */
        *out_count = results.size();
        if (*out_count > 0) {
            *out_pairs = static_cast<AdapterResultPair *>(
                std::malloc(*out_count * sizeof(AdapterResultPair)));
            if (!*out_pairs) {
                g_last_error = "malloc failed for result pairs";
                *out_count = 0;
                return false;
            }
            /* Safe because layout is verified by static_assert above */
            std::memcpy(*out_pairs, results.data(),
                        *out_count * sizeof(AdapterResultPair));
        }
        return true;

    } catch (const FpgaException &e) {
        g_last_error = e.what();
        return false;
    } catch (const std::exception &e) {
        g_last_error = std::string("unexpected exception: ") + e.what();
        return false;
    }
}

const char *fpga_adapter_last_error(void)
{
    return g_last_error.c_str();
}

bool fpga_adapter_last_host_metrics(AdapterHostMetrics *out_metrics)
{
    if (!g_have_last_metrics || out_metrics == nullptr)
        return false;
    *out_metrics = g_last_metrics;
    return true;
}

} /* extern "C" */
