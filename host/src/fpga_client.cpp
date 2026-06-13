#include "fpga_client.hpp"
#include "logger.hpp"
#include "tuple_serializer.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <thread>

static double elapsed_ms(std::chrono::steady_clock::time_point from,
                         std::chrono::steady_clock::time_point to) {
    return std::chrono::duration_cast<std::chrono::microseconds>(to - from).count() / 1000.0;
}

static uint8_t next_session_id() {
    static std::atomic<unsigned> counter{0};
    const unsigned id = counter.fetch_add(1, std::memory_order_relaxed) % 255u;
    return static_cast<uint8_t>(id + 1u);
}

struct SessionCleanup {
    ITransport& transport;
    bool enabled = false;

    ~SessionCleanup() {
        if (!enabled)
            return;
        const uint8_t frame[3] = { MSG_RESET, 0x00, 0x00 };
        try {
            transport.send(frame, sizeof(frame));
        } catch (...) {
        }
    }

    void dismiss() {
        enabled = false;
    }
};

FpgaClient::FpgaClient(ITransport& transport, ClientConfig cfg)
    : transport(transport), cfg(cfg) {
    if (this->cfg.max_batch_tuples == 0 || this->cfg.max_batch_tuples > RX_BUF_TUPLES)
        this->cfg.max_batch_tuples = RX_BUF_TUPLES;
    if (this->cfg.ack_window_frames == 0)
        this->cfg.ack_window_frames = ACK_BATCH_FRAMES;
}

void FpgaClient::send_counted(const void* data,
                              size_t len,
                              MsgType primary_msg,
                              double& bucket_ms) {
    const auto t0 = std::chrono::steady_clock::now();
    transport.send(data, len);
    const auto t1 = std::chrono::steady_clock::now();
    bucket_ms += elapsed_ms(t0, t1);

    metrics.transport_sends++;
    metrics.protocol_frames_sent++;
    metrics.bytes_sent += len;

    switch (primary_msg) {
    case MSG_CONFIGURE:
        metrics.config_frames_sent++;
        break;
    case MSG_INNER_DATA:
        metrics.inner_frames_sent++;
        break;
    case MSG_OUTER_DATA:
        metrics.outer_frames_sent++;
        break;
    case MSG_RESET:
        metrics.reset_frames_sent++;
        break;
    default:
        break;
    }
}

void FpgaClient::account_coalesced_inner_frame(size_t frame_bytes) {
    (void)frame_bytes;
    metrics.protocol_frames_sent++;
    metrics.inner_frames_sent++;
}

void FpgaClient::account_wait(AckWaitKind kind, double ms) {
    switch (kind) {
    case AckWaitKind::Config:
        metrics.config_ack_wait_ms += ms;
        break;
    case AckWaitKind::Build:
        metrics.build_ack_wait_ms += ms;
        break;
    case AckWaitKind::Probe:
        metrics.probe_ack_wait_ms += ms;
        break;
    case AckWaitKind::FinalStatus:
        metrics.final_status_wait_ms += ms;
        break;
    case AckWaitKind::Reset:
        metrics.reset_wait_ms += ms;
        break;
    }
}

static void verify_matched_count(const char* where,
                                 const std::vector<ResultPair>& results,
                                 const AckInfo& ack) {
    if (ack.rows_matched != results.size()) {
        throw FpgaException(std::string("FPGA ") + where +
                            " rows_matched mismatch: status=" +
                            std::to_string(ack.rows_matched) +
                            " results=" + std::to_string(results.size()));
    }
}

static size_t apply_cumulative_ack(std::deque<size_t>& inflight_batches,
                                   size_t& acked_rows,
                                   uint32_t rows_processed) {
    if (rows_processed <= acked_rows)
        return 0u;

    const size_t target_rows = rows_processed;
    size_t acked_frames = 0u;
    while (!inflight_batches.empty() &&
           acked_rows + inflight_batches.front() <= target_rows) {
        acked_rows += inflight_batches.front();
        inflight_batches.pop_front();
        acked_frames++;
    }
    return acked_frames;
}

// ─── Public API ──────────────────────────────────────────────────────────────

std::vector<ResultPair> FpgaClient::run_hash_join(const JoinConfig& config,
                                                   const InputTuple* inner,
                                                   size_t            inner_count,
                                                   const InputTuple* outer,
                                                   size_t            outer_count) {
    TupleSerializer ser(config.key_type);
    credit = 0;
    active_session_id = cfg.session_id_override != 0 ? cfg.session_id_override
                                                     : next_session_id();
    metrics = HostSessionMetrics{};
    SessionCleanup cleanup{transport, cfg.reset_after_run};
    const auto t_start = std::chrono::steady_clock::now();
    auto t_config_done = t_start;
    auto t_build_done = t_start;

    // ── CONFIGURE ────────────────────────────────────────────────────────────
    // Counts come from actual array sizes — planner estimates must not be used here.
    LOG_INFO("FpgaClient", "CONFIGURE alg=%u key=%u inner=%zu outer=%zu",
             config.algorithm, (unsigned)config.key_type, inner_count, outer_count);
    size_t i_sent = 0;
    size_t first_inner_batch = 0;
    {
        auto frame = ser.serialize_configure(config,
                                             static_cast<uint32_t>(inner_count),
                                             static_cast<uint32_t>(outer_count),
                                             active_session_id);
        const bool coalesce_first_inner =
            cfg.coalesce_config_first_inner && inner_count != 0;
        if (coalesce_first_inner) {
            const size_t batch = std::min({(size_t)RX_BUF_TUPLES,
                                           (size_t)cfg.max_batch_tuples,
                                           inner_count});
            auto first_inner = ser.serialize_batch(MSG_INNER_DATA, inner, batch, batch);
            const size_t first_inner_bytes = first_inner.size();
            frame.insert(frame.end(), first_inner.begin(), first_inner.end());
            i_sent = batch;
            first_inner_batch = batch;
            account_coalesced_inner_frame(first_inner_bytes);
        }
        send_counted(frame.data(), frame.size(), MSG_CONFIGURE, metrics.config_send_ms);
        std::vector<ResultPair> unused;
        credit = collect_until_ack(unused, AckWaitKind::Config).credit;
    }
    t_config_done = std::chrono::steady_clock::now();
    LOG_INFO("FpgaClient", "BUILD_START session=%u credit=%u",
             active_session_id, credit);

    // ── BUILD PHASE ───────────────────────────────────────────────────────────
    std::deque<size_t> build_inflight;
    size_t build_acked_rows = 0;
    if (first_inner_batch != 0) {
        build_inflight.push_back(first_inner_batch);
        credit = (credit > first_inner_batch)
            ? static_cast<uint16_t>(credit - first_inner_batch)
            : 0u;
    }
    while (i_sent < inner_count || !build_inflight.empty()) {
        while (i_sent < inner_count &&
               build_inflight.size() < cfg.ack_window_frames &&
               credit != 0) {
            const size_t remaining = inner_count - i_sent;
            if (credit < cfg.max_batch_tuples && remaining > credit)
                break;
            size_t batch = std::min({(size_t)credit,
                                     (size_t)cfg.max_batch_tuples,
                                     remaining});
            auto frame = ser.serialize_batch(MSG_INNER_DATA, inner + i_sent, batch, batch);
            LOG_DEBUG("FpgaClient",
                      "BUILD_SEND offset=%zu batch=%zu inflight_before=%zu credit_before=%u window=%u",
                      i_sent, batch, build_inflight.size(), credit, cfg.ack_window_frames);
            i_sent += batch;
            credit = (credit > batch) ? static_cast<uint16_t>(credit - batch) : 0u;
            build_inflight.push_back(batch);
            send_counted(frame.data(), frame.size(), MSG_INNER_DATA, metrics.build_send_ms);
            if (cfg.inter_batch_gap_ms > 0 && i_sent < inner_count)
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.inter_batch_gap_ms));
        }
        if (build_inflight.empty())
            throw FpgaException("FPGA gave zero credit during build phase");
        std::vector<ResultPair> unused;
        AckInfo build_ack = collect_until_ack(unused, AckWaitKind::Build);
        credit = build_ack.credit;
        const size_t acked_frames =
            apply_cumulative_ack(build_inflight, build_acked_rows,
                                 build_ack.rows_processed);
        LOG_DEBUG("FpgaClient",
                  "BUILD_ACK sent=%zu acked_rows=%zu acked_frames=%zu inflight=%zu credit=%u",
                  i_sent, build_acked_rows, acked_frames, build_inflight.size(),
                  credit);
        if (acked_frames == 0 && !build_inflight.empty()) {
            throw FpgaException("FPGA build ACK did not advance rows_processed");
        }
    }
    LOG_INFO("FpgaClient", "BUILD_DONE sent=%zu credit=%u", i_sent, credit);
    t_build_done = std::chrono::steady_clock::now();
    credit = (uint16_t)RX_CREDIT_TUPLES;
    if (cfg.inter_batch_gap_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.inter_batch_gap_ms));

    // ── PROBE PHASE ───────────────────────────────────────────────────────────
    LOG_INFO("FpgaClient", "PROBE_START outer=%zu credit=%u", outer_count, credit);
    std::vector<ResultPair> results;
    AckInfo last_probe_ack{};
    size_t o_sent = 0;
    size_t probe_acked_rows = 0;
    std::deque<size_t> probe_inflight;
    bool probe_reached_done = false;
    while ((o_sent < outer_count || !probe_inflight.empty()) && !probe_reached_done) {
        while (o_sent < outer_count &&
               probe_inflight.size() < cfg.ack_window_frames &&
               credit != 0) {
            const size_t remaining = outer_count - o_sent;
            if (credit < cfg.max_batch_tuples && remaining > credit)
                break;
            size_t batch = std::min({(size_t)credit,
                                     (size_t)cfg.max_batch_tuples,
                                     remaining});
            auto frame = ser.serialize_batch(MSG_OUTER_DATA, outer + o_sent, batch, batch);
            LOG_DEBUG("FpgaClient",
                      "PROBE_SEND offset=%zu batch=%zu inflight_before=%zu credit_before=%u window=%u",
                      o_sent, batch, probe_inflight.size(), credit, cfg.ack_window_frames);
            o_sent += batch;
            credit = (credit > batch) ? static_cast<uint16_t>(credit - batch) : 0u;
            probe_inflight.push_back(batch);
            send_counted(frame.data(), frame.size(), MSG_OUTER_DATA, metrics.probe_send_ms);
            if (cfg.inter_batch_gap_ms > 0 && o_sent < outer_count)
                std::this_thread::sleep_for(std::chrono::milliseconds(cfg.inter_batch_gap_ms));
        }
        if (probe_inflight.empty())
            throw FpgaException("FPGA gave zero credit during probe phase");
        last_probe_ack = collect_until_ack(results, AckWaitKind::Probe);
        credit = last_probe_ack.credit;
        const size_t acked_frames =
            apply_cumulative_ack(probe_inflight, probe_acked_rows,
                                 last_probe_ack.rows_processed);
        LOG_DEBUG("FpgaClient",
                  "PROBE_ACK sent=%zu acked_rows=%zu acked_frames=%zu inflight=%zu phase=%u credit=%u processed=%u matched=%u results=%zu",
                  o_sent, probe_acked_rows, acked_frames, probe_inflight.size(),
                  last_probe_ack.phase, credit,
                  last_probe_ack.rows_processed, last_probe_ack.rows_matched,
                  results.size());
        if (last_probe_ack.phase == PHASE_DONE) {
            probe_reached_done = true;
            probe_inflight.clear();
        } else if (acked_frames == 0 && !probe_inflight.empty()) {
            throw FpgaException("FPGA probe ACK did not advance rows_processed");
        }
    }

    if (config.algorithm == static_cast<uint8_t>(ALGORITHM_A) &&
        outer_count != 0 &&
        last_probe_ack.phase == PHASE_PROBING &&
        last_probe_ack.credit != 0 &&
        last_probe_ack.rows_processed >= outer_count) {
        const uint8_t probe_end_frame[3] = { MSG_OUTER_DATA, 0x00, 0x00 };
        send_counted(probe_end_frame, sizeof(probe_end_frame),
                     MSG_OUTER_DATA, metrics.probe_send_ms);
        last_probe_ack = collect_until_ack(results, AckWaitKind::Probe);
        credit = last_probe_ack.credit;
    }

    const bool final_probe_ack =
        cfg.infer_done_from_probe_ack &&
        config.algorithm == static_cast<uint8_t>(ALGORITHM_A) &&
        last_probe_ack.phase == PHASE_PROBING &&
        last_probe_ack.credit == 0 &&
        last_probe_ack.rows_processed >= outer_count &&
        last_probe_ack.rows_matched == results.size();

    if (last_probe_ack.phase != PHASE_DONE && final_probe_ack) {
        LOG_INFO("FpgaClient",
                 "PROBE_DONE inferred from final ACK matched=%zu rows=%u; resetting kernel",
                 results.size(), last_probe_ack.rows_processed);
        verify_matched_count("final ACK", results, last_probe_ack);
        send_reset();
        cleanup.dismiss();
        for (unsigned attempts = 0; attempts < 8; ++attempts) {
            std::vector<ResultPair> reset_tail;
            AckInfo reset_ack{};
            try {
                reset_ack = collect_until_ack(reset_tail, AckWaitKind::Reset);
            } catch (const FpgaTimeoutException&) {
                LOG_WARN("FpgaClient", "reset ACK not observed after inferred final ACK");
                break;
            }
            if (reset_ack.phase == PHASE_IDLE)
                break;
            if (attempts == 7)
                throw FpgaException("FPGA reset after final probe ACK did not reach PHASE_IDLE");
            LOG_DEBUG("FpgaClient",
                      "ignoring non-IDLE frame while waiting for reset ACK phase=%u",
                      reset_ack.phase);
        }
    } else if (last_probe_ack.phase != PHASE_DONE) {
        std::vector<ResultPair> tail;
        AckInfo final_ack = collect_until_ack(tail, AckWaitKind::FinalStatus);
        results.insert(results.end(), tail.begin(), tail.end());
        if (final_ack.phase != PHASE_DONE)
            throw FpgaException("FPGA final drain did not reach PHASE_DONE");
        verify_matched_count("final STATUS", results, final_ack);
        LOG_INFO("FpgaClient", "PROBE_DONE matched=%zu phase=%u",
                 results.size(), final_ack.phase);
    } else {
        verify_matched_count("final STATUS", results, last_probe_ack);
        LOG_INFO("FpgaClient", "PROBE_DONE matched=%zu phase=%u",
                 results.size(), last_probe_ack.phase);
    }

    const auto t_done = std::chrono::steady_clock::now();
    metrics.adapter_total_ms = elapsed_ms(t_start, t_done);
    LOG_INFO("FpgaClient",
             "TIMING config_ms=%.3f build_ms=%.3f probe_ms=%.3f total_ms=%.3f results=%zu",
             elapsed_ms(t_start, t_config_done),
             elapsed_ms(t_config_done, t_build_done),
             elapsed_ms(t_build_done, t_done),
             elapsed_ms(t_start, t_done),
             results.size());

    return results;
}

void FpgaClient::send_reset() {
    uint8_t frame[3] = { MSG_RESET, 0x00, 0x00 };
    double reset_send_ms = 0.0;
    send_counted(frame, 3, MSG_RESET, reset_send_ms);
}

// ─── Private ─────────────────────────────────────────────────────────────────

AckInfo FpgaClient::collect_until_ack(std::vector<ResultPair>& out_results,
                                      AckWaitKind kind) {
    using namespace std::chrono;
    auto start  = steady_clock::now();
    bool warned = false;

    while (true) {
        auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
        if (!warned && ms >= static_cast<long long>(cfg.warn_timeout_ms)) {
            LOG_WARN("FpgaClient", "no ACK for %lld ms", ms);
            warned = true;
        }
        if (ms >= static_cast<long long>(cfg.hard_timeout_ms)) {
            LOG_ERROR("FpgaClient", "hard timeout after %lld ms", ms);
            throw FpgaTimeoutException();
        }

        std::vector<ResultPair> pairs;
        AckInfo   ack{};
        FpgaError err = ERR_NONE;
        const auto recv_start = steady_clock::now();
        MsgType   msg = decoder.recv_frame(transport, pairs, ack, err);
        const auto recv_end = steady_clock::now();

        if (msg == MSG_NONE) continue;  // no data yet — loop and re-check watchdog

        metrics.protocol_frames_recv++;
        switch (msg) {
        case MSG_RESULT:
            metrics.result_frames_recv++;
            metrics.result_pairs_recv += pairs.size();
            metrics.bytes_recv += 3u + 12u * pairs.size();
            metrics.result_recv_ms += elapsed_ms(recv_start, recv_end);
            out_results.insert(out_results.end(), pairs.begin(), pairs.end());
            break;
        case MSG_ACK:
            metrics.ack_frames_recv++;
            metrics.bytes_recv += 15u;
            if (kind != AckWaitKind::Reset && ack.session_id != active_session_id) {
                LOG_WARN("FpgaClient",
                         "ignoring stale ACK phase=%u session=%u expected=%u",
                         ack.phase, ack.session_id, active_session_id);
                break;
            }
            account_wait(kind, elapsed_ms(start, recv_end));
            return ack;
        case MSG_STATUS:
            metrics.status_frames_recv++;
            metrics.bytes_recv += 15u;
            if (kind != AckWaitKind::Reset && ack.session_id != active_session_id) {
                LOG_WARN("FpgaClient",
                         "ignoring stale STATUS phase=%u session=%u expected=%u",
                         ack.phase, ack.session_id, active_session_id);
                break;
            }
            account_wait(kind, elapsed_ms(start, recv_end));
            return ack;
        case MSG_ERROR:
            metrics.error_frames_recv++;
            metrics.bytes_recv += 4u;
            throw FpgaErrorException(err);
        case MSG_DEBUG:
            metrics.debug_frames_recv++;
            metrics.bytes_recv += 10u;
            break;
        case MSG_TIMING:
            metrics.timing_frames_recv++;
            metrics.bytes_recv += 3u + TIMING_SUMMARY_BYTES;
            metrics.has_board_timing = decoder.has_timing_summary();
            metrics.board_timing = decoder.timing_summary();
            break;
        default:
            break;  // unknown frame type — ignore
        }
    }
}
