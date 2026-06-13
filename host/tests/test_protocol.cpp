// test_protocol.cpp — verifies ResultDecoder frame parsing using MockTransport
#include "test_helpers.hpp"
#include "mock_transport.hpp"
#include "result_decoder.hpp"
#include <cstdio>
#include <stdexcept>

static ResultDecoder dec;

// ─── Tests ───────────────────────────────────────────────────────────────────

static void test_recv_ack() {
    MockTransport t;
    t.push_response(make_ack(PHASE_BUILDING, 256, 0, 0));

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    MsgType   msg = dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(msg, MSG_ACK);
    CHECK_EQ(ack.phase,          static_cast<uint8_t>(PHASE_BUILDING));
    CHECK_EQ(ack.credit,         static_cast<uint16_t>(256));
    CHECK_EQ(ack.rows_processed, 0u);
    CHECK_EQ(ack.rows_matched,   0u);
    CHECK(t.all_responses_consumed());
    std::printf("  OK  recv_ack\n");
}

static void test_recv_result() {
    MockTransport t;
    ResultPair rp;
    rp.inner_tid = {7, 3};
    rp.outer_tid = {9, 5};
    t.push_response(make_result({rp}));

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    MsgType   msg = dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(msg, MSG_RESULT);
    CHECK_EQ(pairs.size(), 1u);
    CHECK_EQ(pairs[0].inner_tid.blkno, 7u);
    CHECK_EQ(pairs[0].inner_tid.offno, 3u);
    CHECK_EQ(pairs[0].outer_tid.blkno, 9u);
    CHECK_EQ(pairs[0].outer_tid.offno, 5u);
    std::printf("  OK  recv_result\n");
}

static void test_recv_error() {
    MockTransport t;
    t.push_response(make_error(ERR_OVERFLOW));

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    MsgType   msg = dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(msg, MSG_ERROR);
    CHECK_EQ(err, ERR_OVERFLOW);
    std::printf("  OK  recv_error\n");
}

static void test_recv_none_when_empty() {
    MockTransport t;  // nothing queued

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    MsgType   msg = dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(msg, MSG_NONE);
    std::printf("  OK  recv_none_when_empty\n");
}

static void test_recv_multiple_result_pairs() {
    MockTransport t;
    ResultPair rp1{{0, 1}, {1, 1}};
    ResultPair rp2{{0, 2}, {1, 3}};
    t.push_response(make_result({rp1, rp2}));

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(pairs.size(), 2u);
    CHECK_EQ(pairs[0].inner_tid.offno, 1u);
    CHECK_EQ(pairs[1].inner_tid.offno, 2u);
    std::printf("  OK  recv_multiple_result_pairs\n");
}

static void test_partial_frame_times_out() {
    MockTransport t;
    t.push_response({static_cast<uint8_t>(MSG_ACK)});

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;

    bool threw = false;
    try {
        (void)dec.recv_frame(t, pairs, ack, err);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    CHECK(threw);
    std::printf("  OK  partial_frame_times_out\n");
}

static void test_bad_ack_count_rejected() {
    MockTransport t;
    t.push_response({static_cast<uint8_t>(MSG_ACK), 0x02, 0x00});

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;

    bool threw = false;
    try {
        (void)dec.recv_frame(t, pairs, ack, err);
    } catch (const std::runtime_error&) {
        threw = true;
    }

    CHECK(threw);
    std::printf("  OK  bad_ack_count_rejected\n");
}

static void test_recv_timing_summary() {
    MockTransport t;
    TimingSummaryPayload timing{};
    timing.version = TIMING_SUMMARY_VERSION;
    timing.clock_hz = 70000000u;
    timing.inner_rows = 16u;
    timing.outer_rows = 64u;
    timing.matched_rows = 64u;
    timing.inner_frames = 1u;
    timing.outer_frames = 1u;
    timing.result_frames = 1u;
    timing.ack_frames = 3u;
    timing.bytes_rx = 15u + 3u + 16u * 10u + 3u + 64u * 10u;
    timing.bytes_tx = 15u * 3u + 3u + 64u * 12u;
    timing.session_total_cycles = 1234567ull;
    timing.build_compute_cycles = 1600ull;
    timing.probe_compute_cycles = 6400ull;
    timing.result_emit_cycles = 900ull;
    timing.max_probe_batch_cycles = 6400u;
    timing.hash_build_inserts = 16u;
    timing.hash_probe_lookups = 64u;
    timing.hash_probe_hits = 64u;
    timing.hash_probe_misses = 0u;
    timing.hash_probe_collision_steps = 12u;
    timing.hash_max_probe_distance = 2u;
    timing.hash_table_load_factor_ppm = 976u;
    t.push_response(make_timing_summary(timing));

    std::vector<ResultPair> pairs;
    AckInfo   ack{};
    FpgaError err = ERR_NONE;
    MsgType   msg = dec.recv_frame(t, pairs, ack, err);

    CHECK_EQ(msg, MSG_TIMING);
    CHECK(dec.has_timing_summary());
    CHECK_EQ(dec.timing_frames_received(), 1u);
    const auto& got = dec.timing_summary();
    CHECK_EQ(got.version, static_cast<uint16_t>(TIMING_SUMMARY_VERSION));
    CHECK_EQ(got.clock_hz, 70000000u);
    CHECK_EQ(got.inner_rows, 16u);
    CHECK_EQ(got.outer_rows, 64u);
    CHECK_EQ(got.matched_rows, 64u);
    CHECK_EQ(got.session_total_cycles, 1234567ull);
    CHECK_EQ(got.build_compute_cycles, 1600ull);
    CHECK_EQ(got.probe_compute_cycles, 6400ull);
    CHECK_EQ(got.result_emit_cycles, 900ull);
    CHECK_EQ(got.hash_build_inserts, 16u);
    CHECK_EQ(got.hash_probe_lookups, 64u);
    CHECK_EQ(got.hash_probe_hits, 64u);
    CHECK_EQ(got.hash_probe_misses, 0u);
    CHECK_EQ(got.hash_probe_collision_steps, 12u);
    CHECK_EQ(got.hash_max_probe_distance, static_cast<uint16_t>(2u));
    CHECK_EQ(got.hash_table_load_factor_ppm, 976u);
    CHECK(t.all_responses_consumed());
    std::printf("  OK  recv_timing_summary\n");
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    test_recv_ack();
    test_recv_result();
    test_recv_error();
    test_recv_none_when_empty();
    test_recv_multiple_result_pairs();
    test_partial_frame_times_out();
    test_bad_ack_count_rejected();
    test_recv_timing_summary();
    return TEST_RESULT();
}
