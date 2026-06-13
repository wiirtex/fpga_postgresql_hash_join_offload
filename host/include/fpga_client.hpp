#pragma once
#include "fpga_types.hpp"
#include "transport.hpp"
#include "result_decoder.hpp"
#include <stdexcept>
#include <string>
#include <vector>

// ─── Exceptions ──────────────────────────────────────────────────────────────

class FpgaException : public std::runtime_error {
public:
    explicit FpgaException(const std::string& msg) : std::runtime_error(msg) {}
};

class FpgaTimeoutException : public FpgaException {
public:
    FpgaTimeoutException() : FpgaException("FPGA communication timeout") {}
};

class FpgaErrorException : public FpgaException {
public:
    FpgaError error_code;
    explicit FpgaErrorException(FpgaError e)
        : FpgaException("FPGA error code " + std::to_string(e)), error_code(e) {}
};

// ─── Configuration ───────────────────────────────────────────────────────────

struct ClientConfig {
    uint32_t warn_timeout_ms = 2'000;   // log warning if no ACK within this time
    uint32_t hard_timeout_ms = 30'000;  // throw FpgaTimeoutException after this time
    uint16_t max_batch_tuples = RX_BUF_TUPLES;
    uint16_t ack_window_frames = 15;
    uint32_t inter_batch_gap_ms = 0;
    uint8_t session_id_override = 0; // 0 = auto-generate; nonzero is useful in tests
    bool coalesce_config_first_inner = false;
    bool infer_done_from_probe_ack = false;
    bool reset_after_run = false;
};

struct HostSessionMetrics {
    double config_send_ms = 0.0;
    double config_ack_wait_ms = 0.0;
    double build_send_ms = 0.0;
    double build_ack_wait_ms = 0.0;
    double probe_send_ms = 0.0;
    double probe_ack_wait_ms = 0.0;
    double final_status_wait_ms = 0.0;
    double reset_wait_ms = 0.0;
    double result_recv_ms = 0.0;
    double adapter_total_ms = 0.0;

    uint64_t protocol_frames_sent = 0;
    uint64_t protocol_frames_recv = 0;
    uint64_t transport_sends = 0;
    uint64_t bytes_sent = 0;
    uint64_t bytes_recv = 0;

    uint64_t config_frames_sent = 0;
    uint64_t inner_frames_sent = 0;
    uint64_t outer_frames_sent = 0;
    uint64_t reset_frames_sent = 0;
    uint64_t ack_frames_recv = 0;
    uint64_t status_frames_recv = 0;
    uint64_t result_frames_recv = 0;
    uint64_t debug_frames_recv = 0;
    uint64_t timing_frames_recv = 0;
    uint64_t error_frames_recv = 0;
    uint64_t result_pairs_recv = 0;

    bool has_board_timing = false;
    TimingSummaryPayload board_timing{};
};

// ─── FpgaClient ──────────────────────────────────────────────────────────────

// Orchestrates a complete Hash Join session over an ITransport.
// This is the single public facade for Stage 3 (PostgreSQL adapter).
//
// Typical use:
//   FpgaClient client(uart_transport);
//   auto results = client.run_hash_join(cfg, inner.data(), inner.size(),
//                                            outer.data(), outer.size());
//   // results contains all matching (inner_tid, outer_tid) pairs
class FpgaClient {
public:
    FpgaClient(ITransport& transport, ClientConfig cfg = {});

    // Execute a complete Hash Join.
    // Sends CONFIGURE → INNER_DATA batches → OUTER_DATA batches.
    // inner_count / outer_count are the exact sizes of the arrays — the CONFIGURE
    // frame uses these values, not planner estimates.
    // Returns all ResultPair collected during the probe phase.
    // Throws FpgaErrorException on FPGA error, FpgaTimeoutException on timeout.
    std::vector<ResultPair> run_hash_join(const JoinConfig& config,
                                          const InputTuple* inner,
                                          size_t            inner_count,
                                          const InputTuple* outer,
                                          size_t            outer_count);

    // Send MSG_RESET and return without waiting for a response.
    void send_reset();

    const HostSessionMetrics& last_host_metrics() const { return metrics; }

private:
    enum class AckWaitKind {
        Config,
        Build,
        Probe,
        FinalStatus,
        Reset,
    };

    ITransport&   transport;
    ClientConfig  cfg;
    ResultDecoder decoder;
    uint16_t      credit = 0;   // available FPGA RX buffer slots
    uint8_t       active_session_id = 0;
    HostSessionMetrics metrics;

    // Receive frames until MSG_ACK; collect any MSG_RESULT into out_results.
    // Implements the watchdog: logs warning at warn_timeout_ms, throws at hard_timeout_ms.
    AckInfo collect_until_ack(std::vector<ResultPair>& out_results, AckWaitKind kind);

    void send_counted(const void* data, size_t len, MsgType primary_msg, double& bucket_ms);
    void account_coalesced_inner_frame(size_t frame_bytes);
    void account_wait(AckWaitKind kind, double ms);
};
