// uart_smoke.cpp — Low-level board smoke tests using UartTransport directly.
//
// These tests bypass FpgaClient and send/receive raw protocol bytes so that
// failures can be diagnosed at the byte level before the full client is used.
//
// Tests covered:
//   B-01  UART physical connectivity  — send MSG_RESET, expect MSG_ACK
//   B-05  Overflow detection + recovery — CONFIGURE(inner=12289), expect MSG_ERROR,
//          then run a normal join and verify the kernel recovered
//   B-10  Watchdog timeout            — send CONFIGURE, stall, expect silence +
//          RESET→ACK, then verify normal operation
//
// Usage (Windows):
//   uart_smoke.exe COM5
//   uart_smoke.exe COM5 115200
//
// Usage (Linux):
//   ./uart_smoke /dev/ttyUSB0
//   ./uart_smoke /dev/ttyUSB0 115200

#include "uart_transport.hpp"
#include "hash_join_types.hpp"
#include "fpga_client.hpp"
#include "fpga_types.hpp"
#include "logger.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

// ─── Helpers ──────────────────────────────────────────────────────────────────

static int g_pass = 0;
static int g_fail = 0;

#define SMOKE_PASS(name) do { printf("  PASS  %s\n", name); ++g_pass; } while(0)
#define SMOKE_FAIL(name, why) do { printf("  FAIL  %s — %s\n", name, why); ++g_fail; } while(0)

// Write little-endian u8/u16/u32 into a vector.
static void put_u8 (std::vector<uint8_t>& v, uint8_t  x) { v.push_back(x); }
static void put_u16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(x & 0xFF); v.push_back((x >> 8) & 0xFF);
}
static void put_u32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((x >>  0) & 0xFF); v.push_back((x >>  8) & 0xFF);
    v.push_back((x >> 16) & 0xFF); v.push_back((x >> 24) & 0xFF);
}

// Build a CONFIGURE frame.
static std::vector<uint8_t> make_configure(uint8_t  alg,
                                            uint8_t  key_type,
                                            uint32_t inner_count,
                                            uint32_t outer_count) {
    std::vector<uint8_t> f;
    put_u8 (f, MSG_CONFIGURE);
    put_u16(f, 1u);            // count = 1 ConfigurePayload
    put_u8 (f, alg);
    put_u8 (f, key_type);
    put_u16(f, 256u);          // rx_buf_hint (ignored by FPGA)
    put_u32(f, inner_count);
    put_u32(f, outer_count);
    return f;
}

// Single MSG_RESET frame: [0x08][0x00][0x00]
static std::vector<uint8_t> make_reset() {
    return {(uint8_t)MSG_RESET, 0x00u, 0x00u};
}

// Receive bytes until we have at least `need` or the deadline passes.
static std::vector<uint8_t> recv_n(ITransport& t,
                                    size_t need,
                                    uint32_t timeout_ms = 3000) {
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);
    std::vector<uint8_t> buf;
    buf.reserve(need);
    uint8_t tmp[64];
    while (buf.size() < need && clock::now() < deadline) {
        size_t n = t.recv(tmp, std::min(sizeof(tmp), need - buf.size()));
        buf.insert(buf.end(), tmp, tmp + n);
    }
    return buf;
}

// ─── Frame-aware helpers ───────────────────────────────────────────────────────
//
// The kernel prefixes every response with one or more MSG_DEBUG frames
// (e.g. IDLE_ENTER, then the event-specific debug code).  Raw byte scanning
// is unreliable because 0x05 (MSG_ACK) can appear inside a debug payload.
// These helpers read complete framed messages and discard MSG_DEBUG frames.

struct Frame {
    uint8_t              msg_type = 0xFF;   // 0xFF = timeout / error
    std::vector<uint8_t> data;              // full frame bytes incl. 3-byte header
    bool ok() const { return msg_type != 0xFF; }
};

// Read one complete frame (header + payload).  Returns {0xFF,{}} on timeout.
static Frame recv_one_frame(ITransport& t, uint32_t timeout_ms) {
    auto hdr = recv_n(t, 3, timeout_ms);
    if (hdr.size() < 3) return {};

    uint8_t  mt    = hdr[0];
    uint16_t count = static_cast<uint16_t>(hdr[1])
                   | (static_cast<uint16_t>(hdr[2]) << 8);

    // Payload length depends on message type.
    size_t pay = 0;
    switch (static_cast<MsgType>(mt)) {
    case MSG_ACK:
    case MSG_STATUS: pay = 12;           break;  // 1 × StatusPayload
    case MSG_RESULT: pay = count * 12u;  break;  // count × ResultPair
    case MSG_ERROR:  pay = 1;            break;  // 1 × FpgaError byte
    case MSG_DEBUG:  pay = 7;            break;  // 1 × debug record
    case MSG_TIMING: pay = TIMING_SUMMARY_BYTES; break;
    default:         pay = 0;            break;
    }

    Frame f;
    f.msg_type = mt;
    f.data.reserve(3 + pay);
    f.data.insert(f.data.end(), hdr.begin(), hdr.end());
    if (pay > 0) {
        auto pl = recv_n(t, pay, timeout_ms);
        f.data.insert(f.data.end(), pl.begin(), pl.end());
    }
    return f;
}

// Read frames, skipping MSG_DEBUG, and return the first non-debug frame.
// Returns {0xFF,{}} on timeout.
//
// Each recv_one_frame call is given a fixed per-frame budget (1200 ms) so
// recv_n can make at least 2 t.recv() calls (each up to 500 ms) before
// giving up on a single frame.  The outer deadline still bounds total wait.
static Frame recv_skip_debug(ITransport& t, uint32_t timeout_ms = 3000) {
    using clock     = std::chrono::steady_clock;
    using ms        = std::chrono::milliseconds;
    auto deadline   = clock::now() + ms(timeout_ms);
    const uint32_t frame_budget_ms = 1200u;

    for (;;) {
        auto rem = std::chrono::duration_cast<ms>(deadline - clock::now()).count();
        if (rem <= 0) break;
        uint32_t t_ms = static_cast<uint32_t>(
            std::min(static_cast<long long>(frame_budget_ms),
                     static_cast<long long>(rem)));
        auto f = recv_one_frame(t, t_ms);
        if (!f.ok()) continue;  // no bytes — retry until outer deadline
        if (f.msg_type != static_cast<uint8_t>(MSG_DEBUG) &&
            f.msg_type != static_cast<uint8_t>(MSG_TIMING)) return f;
    }
    return {};
}

// Flush both Windows driver buffers and any CP2104 in-flight bytes.
static void purge_uart(ITransport& t) {
    t.reset();
    using clock = std::chrono::steady_clock;
    auto deadline = clock::now() + std::chrono::milliseconds(30);
    uint8_t tmp[64];
    while (clock::now() < deadline)
        t.recv(tmp, sizeof(tmp));
    t.reset();
}

// Send MSG_RESET and wait for MSG_ACK.  Used at end of each test to leave
// the kernel in a clean IDLE state for the next test.
static bool teardown_session(ITransport& t) {
    auto rst = make_reset();
    t.send(rst.data(), rst.size());
    auto f = recv_skip_debug(t, 2000);
    return f.ok() && f.msg_type == static_cast<uint8_t>(MSG_ACK);
}

// Force the kernel to IDLE at program startup when its state is unknown.
// Sends a single MSG_RESET and waits for MSG_ACK; retries up to max_attempts
// times with a purge between each attempt.  Skips MSG_DEBUG and stale
// MSG_ERROR frames while draining.
static bool sync_to_idle(ITransport& t, int max_attempts = 5) {
    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        purge_uart(t);
        auto rst = make_reset();
        t.send(rst.data(), rst.size());

        using clock = std::chrono::steady_clock;
        bool got_ack = false;
        auto deadline = clock::now() + std::chrono::milliseconds(2000);
        while (clock::now() < deadline) {
            auto rem = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - clock::now()).count());
            if (rem == 0) break;
            uint32_t t_ms = std::min(rem, 400u);
            auto f = recv_one_frame(t, t_ms);
            if (!f.ok()) {
                if (got_ack) return true;
                break;
            }
            if (f.msg_type == static_cast<uint8_t>(MSG_ACK)) got_ack = true;
            // Skip MSG_DEBUG and stale MSG_ERROR, keep draining.
        }
        if (got_ack) return true;
    }
    return false;
}

// ─── B-01: UART physical connectivity ─────────────────────────────────────────

static void test_b01(ITransport& t) {
    const char* name = "B-01 UART connectivity (RESET -> ACK)";
    LOG_INFO("uart_smoke", "--- %s ---", name);

    if (!sync_to_idle(t)) {
        SMOKE_FAIL(name, "no response — check UART cable, bitstream, baud rate");
        return;
    }
    SMOKE_PASS(name);
    teardown_session(t);
}

// ─── B-05: Overflow detection + recovery ──────────────────────────────────────

static void test_b05(ITransport& t) {
    const char* name_err  = "B-05a overflow returns MSG_ERROR";
    const char* name_rec  = "B-05b kernel recovers (second join succeeds)";
    LOG_INFO("uart_smoke", "--- %s ---", name_err);

    if (!sync_to_idle(t)) {
        SMOKE_FAIL(name_err, "could not reset kernel to IDLE");
        SMOKE_FAIL(name_rec, "skipped");
        return;
    }

    // Step 1: send CONFIGURE with inner_count = HT_MAX_ROWS + 1 = 12289.
    // Kernel emits MSG_DEBUG(IDLE_ENTER) + MSG_DEBUG(CONFIGURE) then MSG_ERROR.
    {
        auto frame = make_configure((uint8_t)ALGORITHM_A,
                                    (uint8_t)KEY_INT32,
                                    HT_MAX_ROWS + 1,
                                    0u);
        t.send(frame.data(), frame.size());

        auto f = recv_skip_debug(t, 3000);
        if (!f.ok()) {
            printf("    recv_skip_debug timed out\n");
            SMOKE_FAIL(name_err, "no response");
            SMOKE_FAIL(name_rec, "skipped (B-05a failed)");
            teardown_session(t);
            return;
        }
        if (f.msg_type != static_cast<uint8_t>(MSG_ERROR)) {
            printf("    got 0x%02X (%zu bytes), expected 0x%02X (MSG_ERROR)\n",
                   f.msg_type, f.data.size(), static_cast<uint8_t>(MSG_ERROR));
            SMOKE_FAIL(name_err, "expected MSG_ERROR, got something else");
            SMOKE_FAIL(name_rec, "skipped");
            teardown_session(t);
            return;
        }
        SMOKE_PASS(name_err);
    }

    // After overflow, kernel loops back to IDLE automatically.
    // Teardown is not needed here, but sync_to_idle before the FpgaClient
    // ensures any stale bytes (e.g. trailing IDLE_ENTER debug) are purged.
    if (!sync_to_idle(t)) {
        SMOKE_FAIL(name_rec, "could not reset kernel after overflow");
        return;
    }

    // Step 2: run a normal tiny join using FpgaClient — kernel must have recovered.
    // inner = {key=1 TID=(0,1)}, outer = {key=1 TID=(1,1)} → 1 match expected.
    {
        InputTuple inner[1] = {{ 1, {0, 1} }};
        InputTuple outer[1] = {{ 1, {1, 1} }};

        JoinConfig  jcfg;
        jcfg.algorithm = (uint8_t)ALGORITHM_A;
        jcfg.key_type  = KEY_INT32;

        ClientConfig ccfg;
        ccfg.warn_timeout_ms = 1000;
        ccfg.hard_timeout_ms = 5000;

        FpgaClient client(t, ccfg);
        try {
            auto results = client.run_hash_join(jcfg, inner, 1, outer, 1);
            if (results.size() == 1 &&
                results[0].inner_tid.blkno == 0 &&
                results[0].inner_tid.offno == 1 &&
                results[0].outer_tid.blkno == 1 &&
                results[0].outer_tid.offno == 1) {
                SMOKE_PASS(name_rec);
            } else {
                printf("    got %zu results (expected 1)\n", results.size());
                SMOKE_FAIL(name_rec, "wrong result count or TID mismatch");
            }
        } catch (const std::exception& ex) {
            printf("    exception: %s\n", ex.what());
            SMOKE_FAIL(name_rec, ex.what());
        }
    }
    teardown_session(t);
}

// ─── B-10: Watchdog timeout ────────────────────────────────────────────────────

static void test_b10(ITransport& t) {
    const char* name_timeout = "B-10a kernel silent during stall, ACKs RESET";
    const char* name_recover = "B-10b kernel recovers after stall+reset";
    LOG_INFO("uart_smoke", "--- %s ---", name_timeout);

    if (!sync_to_idle(t)) {
        SMOKE_FAIL(name_timeout, "could not reset kernel to IDLE");
        SMOKE_FAIL(name_recover, "skipped");
        return;
    }

    // Step 1: send CONFIGURE(inner=5, outer=5) but never send INNER_DATA.
    // Kernel emits MSG_DEBUG(IDLE_ENTER) + MSG_DEBUG(CONFIGURE) + MSG_ACK.
    // Drain all of that, then stall and verify silence, then send RESET.
    bool b10a_pass = false;
    {
        const uint32_t stall_ms   = 1000;  // time we wait without sending data
        const uint32_t margin_ms  = 200;   // extra headroom

        auto cfg_frame = make_configure((uint8_t)ALGORITHM_A,
                                         (uint8_t)KEY_INT32,
                                         5u,
                                         5u);
        t.send(cfg_frame.data(), cfg_frame.size());

        auto ack = recv_skip_debug(t, 3000);
        if (!ack.ok()) {
            printf("    recv_skip_debug timed out — no ACK for CONFIGURE\n");
            SMOKE_FAIL(name_timeout, "no ACK for CONFIGURE");
            SMOKE_FAIL(name_recover, "skipped");
            teardown_session(t);
            return;
        }
        if (ack.msg_type != static_cast<uint8_t>(MSG_ACK)) {
            printf("    got 0x%02X (%zu bytes), expected MSG_ACK\n",
                   ack.msg_type, ack.data.size());
            SMOKE_FAIL(name_timeout, "unexpected frame type for CONFIGURE ACK");
            SMOKE_FAIL(name_recover, "skipped");
            teardown_session(t);
            return;
        }

        // Stall: kernel waits for INNER_DATA — should be silent.
        using clock = std::chrono::steady_clock;
        auto deadline = clock::now() + std::chrono::milliseconds(stall_ms + margin_ms);
        bool got_unexpected = false;
        uint8_t tmp[64];
        while (clock::now() < deadline) {
            size_t n = t.recv(tmp, sizeof(tmp));
            if (n > 0) { got_unexpected = true; break; }
        }

        if (got_unexpected) {
            SMOKE_FAIL(name_timeout, "kernel sent data during stall — should be silent");
        }

        // Send RESET; kernel is at a frame boundary so it handles it immediately.
        auto rst = make_reset();
        t.send(rst.data(), rst.size());
        auto rst_ack = recv_skip_debug(t, 2000);

        if (!got_unexpected && rst_ack.ok() &&
            rst_ack.msg_type == static_cast<uint8_t>(MSG_ACK)) {
            SMOKE_PASS(name_timeout);
            b10a_pass = true;
        } else if (!got_unexpected) {
            SMOKE_FAIL(name_timeout, "RESET after stall not ACKed");
        }
    }

    // Step 2: verify normal operation after reset.
    // sync_to_idle not needed: the RESET above already left kernel in IDLE.
    if (!b10a_pass) {
        SMOKE_FAIL(name_recover, "skipped (B-10a failed)");
        teardown_session(t);
        return;
    }
    {
        InputTuple inner[1] = {{ 42, {0, 1} }};
        InputTuple outer[1] = {{ 42, {1, 1} }};

        JoinConfig jcfg;
        jcfg.algorithm = (uint8_t)ALGORITHM_A;
        jcfg.key_type  = KEY_INT32;

        ClientConfig ccfg;
        ccfg.warn_timeout_ms = 1000;
        ccfg.hard_timeout_ms = 5000;

        FpgaClient client(t, ccfg);
        try {
            auto results = client.run_hash_join(jcfg, inner, 1, outer, 1);
            if (results.size() == 1) {
                SMOKE_PASS(name_recover);
            } else {
                printf("    got %zu results (expected 1)\n", results.size());
                SMOKE_FAIL(name_recover, "wrong result count");
            }
        } catch (const std::exception& ex) {
            printf("    exception: %s\n", ex.what());
            SMOKE_FAIL(name_recover, ex.what());
        }
    }
    teardown_session(t);
}

// ─── main ─────────────────────────────────────────────────────────────────────

// Algorithm B tiny join through DDR2.
static void test_b20(ITransport& t) {
    const char* name = "B-20 Algorithm B tiny join succeeds";
    LOG_INFO("uart_smoke", "--- %s ---", name);

    if (!sync_to_idle(t)) {
        SMOKE_FAIL(name, "could not reset kernel to IDLE");
        return;
    }

    InputTuple inner[3] = {
        { 1, {2, 1} },
        { 2, {2, 2} },
        { 3, {2, 3} },
    };
    InputTuple outer[4] = {
        { 2, {3, 1} },
        { 9, {3, 2} },
        { 3, {3, 3} },
        { 8, {3, 4} },
    };

    JoinConfig jcfg;
    jcfg.algorithm = (uint8_t)ALGORITHM_B;
    jcfg.key_type  = KEY_INT32;

    ClientConfig ccfg;
    ccfg.warn_timeout_ms = 1000;
    ccfg.hard_timeout_ms = 10000;

    FpgaClient client(t, ccfg);
    try {
        auto results = client.run_hash_join(jcfg, inner, 3, outer, 4);
        bool got_2 = false;
        bool got_3 = false;

        for (const auto& r : results) {
            if (r.inner_tid.blkno == 2 && r.inner_tid.offno == 2 &&
                r.outer_tid.blkno == 3 && r.outer_tid.offno == 1)
                got_2 = true;
            if (r.inner_tid.blkno == 2 && r.inner_tid.offno == 3 &&
                r.outer_tid.blkno == 3 && r.outer_tid.offno == 3)
                got_3 = true;
        }

        if (results.size() == 2 && got_2 && got_3)
            SMOKE_PASS(name);
        else {
            printf("    got %zu results, expected 2 matching keys 2 and 3\n",
                   results.size());
            SMOKE_FAIL(name, "wrong Algorithm B result set");
        }
    } catch (const std::exception& ex) {
        printf("    exception: %s\n", ex.what());
        SMOKE_FAIL(name, ex.what());
    }

    teardown_session(t);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <port> [baud_rate]\n", argv[0]);
        fprintf(stderr, "  Windows: %s COM5\n",         argv[0]);
        fprintf(stderr, "  Linux:   %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }

    const char* port      = argv[1];
    uint32_t    baud_rate = (argc >= 3) ? (uint32_t)atoi(argv[2]) : 115200u;

    printf("uart_smoke — board smoke tests\n");
    printf("  port:      %s\n", port);
    printf("  baud rate: %u\n\n", baud_rate);

    UartTransport uart(port, baud_rate, /*recv_timeout_ms=*/500);

    test_b01(uart);
    test_b05(uart);
    test_b10(uart);
    test_b20(uart);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
