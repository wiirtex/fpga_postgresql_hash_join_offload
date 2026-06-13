// test_client.cpp — end-to-end round-trip test through MockTransport
//
// Scenario (int32 keys):
//   inner = {(1,{0,1}), (2,{0,2}), (3,{0,3})}
//   outer = {(2,{1,1}), (5,{1,2}), (3,{1,3}), (7,{1,4})}
//   Expected matches: (inner_tid={0,2}, outer_tid={1,1})
//                     (inner_tid={0,3}, outer_tid={1,3})
#include "test_helpers.hpp"
#include "mock_transport.hpp"
#include "fpga_client.hpp"
#include <cstdio>
#include <vector>

int main() {
    // ── Test data ─────────────────────────────────────────────────────────────
    InputTuple inner[3] = {
        {1, {0, 1}},
        {2, {0, 2}},
        {3, {0, 3}},
    };
    InputTuple outer[4] = {
        {2, {1, 1}},
        {5, {1, 2}},
        {3, {1, 3}},
        {7, {1, 4}},
    };

    // ── Expected results ──────────────────────────────────────────────────────
    ResultPair expected[2] = {
        {{0, 2}, {1, 1}},   // key=2 matched
        {{0, 3}, {1, 3}},   // key=3 matched
    };

    // ── Pre-populate mock FPGA responses ─────────────────────────────────────
    //
    // Protocol flow (all inner fits in one batch, all outer fits in one batch):
    //   host → CONFIGURE(3 inner, 4 outer, int32)
    //   fpga → ACK(BUILDING, credit=512)
    //   host → INNER_DATA(3 tuples)
    //   fpga → ACK(BUILDING, credit=512, processed=3)
    //   host → OUTER_DATA(4 tuples)
    //   fpga → RESULT(2 pairs)
    //   fpga → ACK(PROBING, credit=512, processed=4, matched=2)
    //   host → OUTER_DATA(count=0) explicit probe-end marker
    //   fpga → TIMING
    //   fpga → STATUS(DONE, matched=2)

    MockTransport t;
    t.push_response(make_ack(PHASE_IDLE, 0, 0, 0, 42));                 // stale ACK from prior session
    t.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));   // ACK for CONFIGURE
    t.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 3, 0));   // ACK for INNER_DATA
    t.push_response(make_result({expected[0], expected[1]}));
    t.push_response(make_ack(PHASE_PROBING,  RX_CREDIT_TUPLES, 4, 2));   // ACK for OUTER_DATA
    TimingSummaryPayload timing{};
    timing.version = TIMING_SUMMARY_VERSION;
    timing.flags = TIMING_FLAG_ESTIMATED_CYCLES;
    timing.clock_hz = TIMING_DEFAULT_CLOCK_HZ;
    timing.inner_rows = 3u;
    timing.outer_rows = 4u;
    timing.matched_rows = 2u;
    timing.session_total_cycles = 1234u;
    t.push_response(make_timing_summary(timing));
    t.push_response(make_status(PHASE_DONE,  2));            // final STATUS(DONE)

    // ── Run the join ──────────────────────────────────────────────────────────
    JoinConfig cfg;
    cfg.algorithm = 0;
    cfg.key_type  = KEY_INT32;

    // Short timeout so test fails fast if MockTransport is exhausted unexpectedly
    ClientConfig ccfg;
    ccfg.warn_timeout_ms = 5;
    ccfg.hard_timeout_ms = 20;
    ccfg.session_id_override = 1;

    FpgaClient client(t, ccfg);
    auto results = client.run_hash_join(cfg, inner, 3, outer, 4);

    // ── Assertions ────────────────────────────────────────────────────────────
    CHECK_EQ(results.size(), 2u);
    if (results.size() == 2) {
        CHECK_EQ(results[0].inner_tid.blkno, 0u);
        CHECK_EQ(results[0].inner_tid.offno, 2u);
        CHECK_EQ(results[0].outer_tid.blkno, 1u);
        CHECK_EQ(results[0].outer_tid.offno, 1u);

        CHECK_EQ(results[1].inner_tid.blkno, 0u);
        CHECK_EQ(results[1].inner_tid.offno, 3u);
        CHECK_EQ(results[1].outer_tid.blkno, 1u);
        CHECK_EQ(results[1].outer_tid.offno, 3u);
    }

    CHECK(t.all_responses_consumed());

    // ── Verify send sequence (spot-check headers) ─────────────────────────────
    const auto& sent = t.sent();
    // First byte must be MSG_CONFIGURE
    CHECK(!sent.empty() && sent[0] == MSG_CONFIGURE);

    std::printf("  Total bytes sent to mock FPGA: %zu\n", sent.size());
    // CONFIGURE(16) + INNER_DATA(3+3*10=33) + OUTER_DATA(3+4*10=43)
    // + OUTER_DATA(count=0)=3 = 95 bytes
    CHECK_EQ(sent.size(), 16u + 33u + 43u + 3u);

    const auto& metrics = client.last_host_metrics();
    CHECK_EQ(metrics.protocol_frames_sent, 4u);
    CHECK_EQ(metrics.transport_sends, 4u);
    CHECK_EQ(metrics.bytes_sent, 16u + 33u + 43u + 3u);
    CHECK_EQ(metrics.config_frames_sent, 1u);
    CHECK_EQ(metrics.inner_frames_sent, 1u);
    CHECK_EQ(metrics.outer_frames_sent, 2u);
    CHECK_EQ(metrics.protocol_frames_recv, 7u);
    CHECK_EQ(metrics.ack_frames_recv, 4u);
    CHECK_EQ(metrics.status_frames_recv, 1u);
    CHECK_EQ(metrics.result_frames_recv, 1u);
    CHECK_EQ(metrics.timing_frames_recv, 1u);
    CHECK_EQ(metrics.result_pairs_recv, 2u);
    CHECK_EQ(metrics.bytes_recv,
             15u + 15u + 15u + (3u + 2u * 12u) + 15u +
             (3u + TIMING_SUMMARY_BYTES) + 15u);
    CHECK(metrics.has_board_timing);
    CHECK_EQ(metrics.board_timing.version, TIMING_SUMMARY_VERSION);
    CHECK_EQ(metrics.board_timing.flags, TIMING_FLAG_ESTIMATED_CYCLES);
    CHECK_EQ(metrics.board_timing.session_total_cycles, 1234ull);

    // ack_window_frames keeps several DATA frames in flight while remaining
    // compatible with boards that still ACK every frame.
    {
        std::vector<InputTuple> w_inner(300);
        std::vector<InputTuple> w_outer(300);
        for (size_t i = 0; i < w_inner.size(); ++i) {
            w_inner[i] = {static_cast<int64_t>(1000 + i), {20u, static_cast<uint16_t>(i)}};
            w_outer[i] = {static_cast<int64_t>(2000 + i), {21u, static_cast<uint16_t>(i)}};
        }

        MockTransport win;
        win.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        win.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 100, 0));
        win.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 200, 0));
        win.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 300, 0));
        win.push_response(make_ack(PHASE_PROBING, RX_CREDIT_TUPLES, 100, 0));
        win.push_response(make_ack(PHASE_PROBING, RX_CREDIT_TUPLES, 200, 0));
        win.push_response(make_timing_summary(timing));
        win.push_response(make_status(PHASE_DONE, 0));

        ClientConfig win_ccfg = ccfg;
        win_ccfg.max_batch_tuples = 100;
        win_ccfg.ack_window_frames = 2;
        FpgaClient win_client(win, win_ccfg);
        auto win_results = win_client.run_hash_join(
            cfg, w_inner.data(), w_inner.size(), w_outer.data(), w_outer.size());

        CHECK_EQ(win_results.size(), 0u);
        CHECK(win.all_responses_consumed());
        const auto& win_send_lengths = win.send_lengths();
        CHECK_EQ(win_send_lengths.size(), 7u);
        if (win_send_lengths.size() == 7u) {
            CHECK_EQ(win_send_lengths[0], 16u);
            CHECK_EQ(win_send_lengths[1], 1003u);
            CHECK_EQ(win_send_lengths[2], 1003u);
            CHECK_EQ(win_send_lengths[3], 1003u);
            CHECK_EQ(win_send_lengths[4], 1003u);
            CHECK_EQ(win_send_lengths[5], 1003u);
            CHECK_EQ(win_send_lengths[6], 1003u);
        }
    }

    // ── Hardware-observed completion path ───────────────────────────────────
    //
    // The current board can finish the final probe batch with
    // ACK(PROBING, credit=0, processed=outer_count), without a following
    // STATUS(DONE).  FpgaClient must accept only that narrow final ACK shape,
    // then send RESET and wait for ACK(IDLE) before returning to the caller.
    {
        InputTuple hw_inner[1] = {
            {42, {2, 1}},
        };
        InputTuple hw_outer[1] = {
            {42, {3, 1}},
        };
        ResultPair hw_expected = {{2, 1}, {3, 1}};

        MockTransport hw;
        hw.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        hw.push_response(make_ack(PHASE_BUILDING, 0, 1, 0));
        hw.push_response(make_result({hw_expected}));
        hw.push_response(make_ack(PHASE_PROBING, 0, 1, 1));
        hw.push_response(make_status(PHASE_DONE, 1));
        hw.push_response(make_ack(PHASE_IDLE, 0, 1, 1));

        ClientConfig hw_ccfg = ccfg;
        hw_ccfg.infer_done_from_probe_ack = true;
        FpgaClient hw_client(hw, hw_ccfg);
        auto hw_results = hw_client.run_hash_join(cfg, hw_inner, 1, hw_outer, 1);

        CHECK_EQ(hw_results.size(), 1u);
        if (hw_results.size() == 1) {
            CHECK_EQ(hw_results[0].inner_tid.blkno, 2u);
            CHECK_EQ(hw_results[0].inner_tid.offno, 1u);
            CHECK_EQ(hw_results[0].outer_tid.blkno, 3u);
            CHECK_EQ(hw_results[0].outer_tid.offno, 1u);
        }
        CHECK(hw.all_responses_consumed());

        const auto& hw_sent = hw.sent();
        CHECK_EQ(hw_sent.size(), 16u + 13u + 13u + 3u);
        if (hw_sent.size() >= 3) {
            CHECK_EQ(hw_sent[hw_sent.size() - 3], static_cast<uint8_t>(MSG_RESET));
            CHECK_EQ(hw_sent[hw_sent.size() - 2], 0x00u);
            CHECK_EQ(hw_sent[hw_sent.size() - 1], 0x00u);
        }
    }

    // ── Algorithm B completion path ─────────────────────────────────────────
    //
    // For Grace Hash Join, ACK(PROBING, credit=0) means the FPGA accepted the
    // last OUTER_DATA partition frame.  Results are emitted after that from
    // DDR2, followed by STATUS(DONE).  The Algorithm A fallback must not reset
    // the kernel early for this path.
    {
        InputTuple b_inner[1] = {
            {77, {4, 1}},
        };
        InputTuple b_outer[1] = {
            {77, {5, 1}},
        };
        ResultPair b_expected = {{4, 1}, {5, 1}};

        JoinConfig b_cfg;
        b_cfg.algorithm = static_cast<uint8_t>(ALGORITHM_B);
        b_cfg.key_type = KEY_INT32;

        MockTransport b;
        b.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        b.push_response(make_ack(PHASE_BUILDING, 0, 1, 0));
        b.push_response(make_ack(PHASE_PROBING, 0, 1, 0));
        b.push_response(make_result({b_expected}));
        b.push_response(make_status(PHASE_DONE, 1));

        ClientConfig b_ccfg = ccfg;
        b_ccfg.coalesce_config_first_inner = true;

        FpgaClient b_client(b, b_ccfg);
        auto b_results = b_client.run_hash_join(b_cfg, b_inner, 1, b_outer, 1);

        CHECK_EQ(b_results.size(), 1u);
        if (b_results.size() == 1) {
            CHECK_EQ(b_results[0].inner_tid.blkno, 4u);
            CHECK_EQ(b_results[0].inner_tid.offno, 1u);
            CHECK_EQ(b_results[0].outer_tid.blkno, 5u);
            CHECK_EQ(b_results[0].outer_tid.offno, 1u);
        }
        CHECK(b.all_responses_consumed());

        const auto& b_sent = b.sent();
        CHECK_EQ(b_sent.size(), 16u + 13u + 13u);
        const auto& b_send_lengths = b.send_lengths();
        CHECK_EQ(b_send_lengths.size(), 2u);
        if (b_send_lengths.size() == 2u) {
            CHECK_EQ(b_send_lengths[0], 16u + 13u);
            CHECK_EQ(b_send_lengths[1], 13u);
        }
        if (b_sent.size() >= 1)
            CHECK_NEQ(b_sent.back(), static_cast<uint8_t>(MSG_RESET));
    }

    // Final STATUS/ACK rows_matched must agree with the actually received
    // RESULT frames.  Otherwise a lost UDP datagram or malformed stream could
    // silently return an incomplete join result.
    {
        InputTuple m_inner[1] = {
            {99, {6, 1}},
        };
        InputTuple m_outer[1] = {
            {99, {7, 1}},
        };

        MockTransport mismatch;
        mismatch.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        mismatch.push_response(make_ack(PHASE_BUILDING, 0, 1, 0));
        mismatch.push_response(make_ack(PHASE_PROBING, 0, 1, 0));
        mismatch.push_response(make_status(PHASE_DONE, 1));

        JoinConfig mismatch_cfg;
        mismatch_cfg.algorithm = static_cast<uint8_t>(ALGORITHM_B);
        mismatch_cfg.key_type = KEY_INT32;

        FpgaClient mismatch_client(mismatch, ccfg);
        bool threw = false;
        try {
            (void)mismatch_client.run_hash_join(mismatch_cfg, m_inner, 1, m_outer, 1);
        } catch (const FpgaException&) {
            threw = true;
        }
        CHECK(threw);
    }

    // UDP board runs should leave the kernel ready for the next request.
    // reset_after_run sends MSG_RESET after a normal STATUS(DONE) path.
    {
        InputTuple c_inner[1] = {
            {11, {8, 1}},
        };
        InputTuple c_outer[1] = {
            {11, {9, 1}},
        };
        ResultPair c_expected = {{8, 1}, {9, 1}};

        MockTransport clean;
        clean.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        clean.push_response(make_ack(PHASE_BUILDING, 0, 1, 0));
        clean.push_response(make_result({c_expected}));
        clean.push_response(make_ack(PHASE_PROBING, 0, 1, 1));
        clean.push_response(make_status(PHASE_DONE, 1));

        ClientConfig clean_ccfg = ccfg;
        clean_ccfg.reset_after_run = true;

        {
            FpgaClient clean_client(clean, clean_ccfg);
            auto clean_results = clean_client.run_hash_join(cfg, c_inner, 1, c_outer, 1);
            CHECK_EQ(clean_results.size(), 1u);
        }

        const auto& clean_sent = clean.sent();
        CHECK(clean_sent.size() >= 3u);
        if (clean_sent.size() >= 3u) {
            CHECK_EQ(clean_sent[clean_sent.size() - 3], static_cast<uint8_t>(MSG_RESET));
            CHECK_EQ(clean_sent[clean_sent.size() - 2], 0x00u);
            CHECK_EQ(clean_sent[clean_sent.size() - 1], 0x00u);
        }
    }

    // The cleanup guard also fires on errors, so a failed board request should
    // not leave the kernel stuck mid-session.
    {
        InputTuple e_inner[1] = {
            {12, {10, 1}},
        };
        InputTuple e_outer[1] = {
            {12, {11, 1}},
        };

        MockTransport err_clean;
        err_clean.push_response(make_ack(PHASE_BUILDING, RX_CREDIT_TUPLES, 0, 0));
        err_clean.push_response(make_error(ERR_OVERFLOW));

        ClientConfig err_ccfg = ccfg;
        err_ccfg.reset_after_run = true;

        bool threw = false;
        try {
            FpgaClient err_client(err_clean, err_ccfg);
            (void)err_client.run_hash_join(cfg, e_inner, 1, e_outer, 1);
        } catch (const FpgaErrorException&) {
            threw = true;
        }
        CHECK(threw);

        const auto& err_sent = err_clean.sent();
        CHECK(err_sent.size() >= 3u);
        if (err_sent.size() >= 3u) {
            CHECK_EQ(err_sent[err_sent.size() - 3], static_cast<uint8_t>(MSG_RESET));
            CHECK_EQ(err_sent[err_sent.size() - 2], 0x00u);
            CHECK_EQ(err_sent[err_sent.size() - 1], 0x00u);
        }
    }

    return TEST_RESULT();
}
