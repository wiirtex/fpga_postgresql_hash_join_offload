#include "hash_join_kernel.hpp"
#include "hash_join_linear.hpp"
#include "hash_join_grace.hpp"

static inline uint64_t cycles_between(uint64_t start, uint64_t finish) {
#pragma HLS INLINE
    return (start != 0ull && finish >= start) ? (finish - start) : 0ull;
}

static inline void apply_rtl_stream_timing(
    TimingSummaryPayload& timing,
    uint32_t rtl_timing_valid,
    uint32_t rtl_timing_clock_hz,
    uint32_t rtl_inner_frames,
    uint32_t rtl_outer_frames,
    uint32_t rtl_result_frames,
    uint32_t rtl_ack_frames,
    uint32_t rtl_debug_frames,
    uint32_t rtl_bytes_rx,
    uint32_t rtl_bytes_tx,
    uint64_t rtl_cycle_counter,
    uint64_t rtl_ts_config_first,
    uint64_t rtl_ts_config_ack,
    uint64_t rtl_ts_build_first,
    uint64_t rtl_ts_build_last,
    uint64_t rtl_ts_build_ack_last,
    uint64_t rtl_ts_probe_first,
    uint64_t rtl_ts_probe_last,
    uint64_t rtl_ts_probe_ack_last,
    uint64_t rtl_ts_result_first,
    uint64_t rtl_ts_result_last) {
#pragma HLS INLINE
    const bool monitor_seen =
        (rtl_ts_config_first != 0ull) &&
        (rtl_cycle_counter >= rtl_ts_config_first);
    if (!monitor_seen) {
        return;
    }

    timing.flags = TIMING_FLAG_RTL_STREAM_CYCLES;
    if (rtl_timing_valid == 0u) {
        timing.flags |= TIMING_FLAG_ESTIMATED_CYCLES;
    }
    timing.clock_hz = (rtl_timing_clock_hz != 0u)
                          ? rtl_timing_clock_hz
                          : TIMING_DEFAULT_CLOCK_HZ;

    timing.inner_frames = rtl_inner_frames;
    timing.outer_frames = rtl_outer_frames;
    timing.result_frames = rtl_result_frames;
    timing.ack_frames = rtl_ack_frames;
    timing.debug_frames = rtl_debug_frames;
    timing.bytes_rx = rtl_bytes_rx;
    timing.bytes_tx = rtl_bytes_tx;

    timing.session_total_cycles =
        cycles_between(rtl_ts_config_first, rtl_cycle_counter);
    timing.config_cycles =
        cycles_between(rtl_ts_config_first, rtl_ts_config_ack);
    timing.build_rx_cycles =
        cycles_between(rtl_ts_build_first, rtl_ts_build_last);
    timing.build_compute_cycles =
        cycles_between(rtl_ts_build_last, rtl_ts_build_ack_last);
    timing.build_total_cycles =
        cycles_between(rtl_ts_build_first, rtl_ts_build_ack_last);
    timing.probe_rx_cycles =
        cycles_between(rtl_ts_probe_first, rtl_ts_probe_last);
    timing.probe_compute_cycles =
        cycles_between(rtl_ts_probe_last, rtl_ts_result_first);
    timing.result_emit_cycles =
        cycles_between(rtl_ts_result_first, rtl_ts_result_last);

    const uint64_t probe_done =
        (rtl_ts_probe_ack_last != 0ull) ? rtl_ts_probe_ack_last : rtl_cycle_counter;
    timing.probe_total_cycles =
        cycles_between(rtl_ts_probe_first, probe_done);
}

static inline void flush_result_batch(
    ByteStream& tx,
    ResultPair result_buf[TX_BUF_SIZE],
    uint16_t& result_buf_cnt,
    TimingSummaryPayload& timing,
    uint64_t& probe_batch_cycles) {
#pragma HLS INLINE
    const uint16_t emitted_results = result_buf_cnt;
    if (emitted_results == 0u) {
        return;
    }

    const uint32_t result_frame_bytes = 3u + (uint32_t)emitted_results * 12u;
    tx_results(tx, result_buf, emitted_results);
    timing.result_frames++;
    timing.bytes_tx += result_frame_bytes;
    timing.result_emit_cycles += result_frame_bytes;
    probe_batch_cycles += result_frame_bytes;
    if (result_frame_bytes > timing.max_result_frame_cycles) {
        timing.max_result_frame_cycles = result_frame_bytes;
    }
    result_buf_cnt = 0u;
}

// ─── Top-level HLS kernel ─────────────────────────────────────────────────────
//
// ap_ctrl_none: no ap_start/ap_done/ap_idle signals.
// The kernel runs continuously in an infinite session loop.
// Each iteration = one join session (CONFIGURE → BUILD → PROBE → DONE).
// On error or RESET the session is abandoned and the loop restarts at IDLE.

void hash_join_kernel(
    ByteStream&        rx,
    ByteStream&        tx,
    volatile uint32_t& status_reg,
    volatile uint32_t& result_count_reg,
    volatile uint32_t& error_code_reg,
    volatile uint32_t& ht_capacity_reg,
    volatile uint32_t& rx_buf_cap_reg,
    uint32_t           rtl_timing_valid,
    uint32_t           rtl_timing_clock_hz,
    uint32_t           rtl_inner_frames,
    uint32_t           rtl_outer_frames,
    uint32_t           rtl_result_frames,
    uint32_t           rtl_ack_frames,
    uint32_t           rtl_debug_frames,
    uint32_t           rtl_bytes_rx,
    uint32_t           rtl_bytes_tx,
    uint64_t           rtl_cycle_counter,
    uint64_t           rtl_ts_config_first,
    uint64_t           rtl_ts_config_ack,
    uint64_t           rtl_ts_build_first,
    uint64_t           rtl_ts_build_last,
    uint64_t           rtl_ts_build_ack_last,
    uint64_t           rtl_ts_probe_first,
    uint64_t           rtl_ts_probe_last,
    uint64_t           rtl_ts_probe_ack_last,
    uint64_t           rtl_ts_result_first,
    uint64_t           rtl_ts_result_last,
    int64_t*           ddr2_buf
) {
// ── HLS interface pragmas ────────────────────────────────────────────────────
#pragma HLS INTERFACE axis         port=rx
#pragma HLS INTERFACE axis         port=tx
#pragma HLS INTERFACE s_axilite    port=status_reg        bundle=ctrl
#pragma HLS INTERFACE s_axilite    port=result_count_reg  bundle=ctrl
#pragma HLS INTERFACE s_axilite    port=error_code_reg    bundle=ctrl
#pragma HLS INTERFACE s_axilite    port=ht_capacity_reg   bundle=ctrl
#pragma HLS INTERFACE s_axilite    port=rx_buf_cap_reg    bundle=ctrl
#pragma HLS INTERFACE ap_none      port=rtl_timing_valid
#pragma HLS INTERFACE ap_none      port=rtl_timing_clock_hz
#pragma HLS INTERFACE ap_none      port=rtl_inner_frames
#pragma HLS INTERFACE ap_none      port=rtl_outer_frames
#pragma HLS INTERFACE ap_none      port=rtl_result_frames
#pragma HLS INTERFACE ap_none      port=rtl_ack_frames
#pragma HLS INTERFACE ap_none      port=rtl_debug_frames
#pragma HLS INTERFACE ap_none      port=rtl_bytes_rx
#pragma HLS INTERFACE ap_none      port=rtl_bytes_tx
#pragma HLS INTERFACE ap_none      port=rtl_cycle_counter
#pragma HLS INTERFACE ap_none      port=rtl_ts_config_first
#pragma HLS INTERFACE ap_none      port=rtl_ts_config_ack
#pragma HLS INTERFACE ap_none      port=rtl_ts_build_first
#pragma HLS INTERFACE ap_none      port=rtl_ts_build_last
#pragma HLS INTERFACE ap_none      port=rtl_ts_build_ack_last
#pragma HLS INTERFACE ap_none      port=rtl_ts_probe_first
#pragma HLS INTERFACE ap_none      port=rtl_ts_probe_last
#pragma HLS INTERFACE ap_none      port=rtl_ts_probe_ack_last
#pragma HLS INTERFACE ap_none      port=rtl_ts_result_first
#pragma HLS INTERFACE ap_none      port=rtl_ts_result_last
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS INTERFACE m_axi        port=ddr2_buf bundle=DDR2 \
    depth=7864320 latency=100 \
    max_read_burst_length=256 max_write_burst_length=256

    // Static instances persist across sessions (across loop iterations).
    // reset() is called explicitly at the appropriate point in each session.
    static LinearHashTable ht;
    static GraceHashEngine grace;

    // Constant capability registers — written once at startup, never change.
    ht_capacity_reg = HT_MAX_ROWS;
    rx_buf_cap_reg  = RX_CREDIT_TUPLES;

    // Result batch buffer declared outside the loop — HLS allocates it once
    // as a fixed resource. result_buf_cnt is reset at the start of each session.
    ResultPair result_buf[TX_BUF_SIZE];
    int64_t rx_key_buf[RX_BUF_TUPLES];
    Tid     rx_tid_buf[RX_BUF_TUPLES];

    // ── Infinite session loop ────────────────────────────────────────────────
    session_loop:
    while (true) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=1

        // ── Session variables (re-initialised every iteration) ───────────────
        uint8_t  algorithm    = (uint8_t)ALGORITHM_A;
        KeyType  keytype      = KEY_INT32;
        uint32_t inner_total  = 0u;
        uint32_t outer_total  = 0u;
        uint32_t matched      = 0u;
        uint32_t K            = 1u;
        uint32_t outer_slot_w = 0u;
        uint16_t result_buf_cnt = 0u;
        uint8_t  session_id = 0u;
        bool     session_done   = false;  // set to true on error/RESET to skip to next session
        TimingSummaryPayload timing = {};
        timing.version = TIMING_SUMMARY_VERSION;
        timing.flags = TIMING_FLAG_ESTIMATED_CYCLES;
        timing.clock_hz = TIMING_DEFAULT_CLOCK_HZ;

        // Initialise status registers
        status_reg       = PHASE_IDLE;
        error_code_reg   = ERR_NONE;
        result_count_reg = 0u;

        emit_debug(tx, DBG_INFO, DBG_IDLE_ENTER, 0u);

        // ── CONFIGURE frame ──────────────────────────────────────────────────
        // Expected: [MSG_CONFIGURE:1B][1:2B][ConfigurePayload:13B]
        if (!session_done) {
            uint8_t  msg_type = rx_byte_guarded(rx, tx, session_done);
            uint16_t count    = 0;
            if (!session_done) count = rx_u16(rx);

            if (!session_done && msg_type == (uint8_t)MSG_RESET) {
                emit_debug(tx, DBG_INFO, DBG_RESET, 0u);
                ht.reset();
                tx_status_frame(tx, MSG_ACK, PHASE_IDLE, 0u, 0u, RX_CREDIT_TUPLES);
                session_done = true;
            } else if (!session_done && (msg_type != (uint8_t)MSG_CONFIGURE || count != 1u)) {
                tx_error(tx, ERR_INVALID_CMD);
                error_code_reg = ERR_INVALID_CMD;
                session_done = true;
            } else if (!session_done) {
                // Deserialise ConfigurePayload (12 bytes, little-endian)
                algorithm             = rx_byte(rx);
                keytype               = (KeyType)rx_byte(rx);
                uint16_t rx_buf_hint  = rx_u16(rx);
                (void)rx_buf_hint;
                inner_total           = rx_u32(rx);
                outer_total           = rx_u32(rx);
                session_id            = rx_byte(rx);
                timing.inner_rows = inner_total;
                timing.outer_rows = outer_total;
                timing.bytes_rx += 16u;
                timing.config_cycles += 16u;

                // Write outer_total to a volatile register immediately so the
                // synthesizer cannot eliminate the stream read (same class of
                // bug as rx_buf_hint).  Overwritten with the real result at DONE.
                result_count_reg = outer_total;

                emit_debug(tx, DBG_INFO, DBG_CONFIGURE, inner_total);

                if (algorithm == (uint8_t)ALGORITHM_A) {
                    if (inner_total > HT_MAX_ROWS) {
                        tx_error(tx, ERR_OVERFLOW);
                        error_code_reg = ERR_OVERFLOW;
                        session_done = true;
                    }
                } else if (algorithm == (uint8_t)ALGORITHM_B) {
                    if (inner_total > GRACE_MAX_INNER_ROWS) {
                        tx_error(tx, ERR_OVERFLOW);
                        error_code_reg = ERR_OVERFLOW;
                        session_done = true;
                    } else {
                        K = (inner_total + HT_MAX_ROWS - 1u) / HT_MAX_ROWS;
                        if (K == 0u) K = 1u;
                        outer_slot_w = ((outer_total / K) + 1u) * GRACE_WORDS_PER_TUPLE * 2u;
                        if (GRACE_OUTER_AREA_BASE + (uint64_t)K * outer_slot_w > GRACE_DDR2_TOTAL_WORDS) {
                            tx_error(tx, ERR_OVERFLOW);
                            error_code_reg = ERR_OVERFLOW;
                            session_done = true;
                        }
                    }
                } else {
                    tx_error(tx, ERR_INVALID_CMD);
                    error_code_reg = ERR_INVALID_CMD;
                    session_done = true;
                }

                if (!session_done) {
                    status_reg = PHASE_BUILDING;
                    timing.ack_frames++;
                    timing.bytes_tx += 15u;
                    timing.ack_emit_cycles += 15u;
                    timing.config_cycles += 15u;
                    tx_status_frame(tx, MSG_ACK, PHASE_BUILDING, 0u, 0u,
                                    RX_CREDIT_TUPLES, session_id);
                }
            }
        }

        // ── Algorithm B: Grace Hash Join ─────────────────────────────────────
        if (!session_done && algorithm == (uint8_t)ALGORITHM_B) {

            {
                const FpgaError e = grace.build_inner_partitions(
                    rx, tx, inner_total, keytype, K, ddr2_buf, session_id);
                if (e != ERR_NONE) {
                    error_code_reg = (uint32_t)e;
                    session_done = true;
                }
            }

            if (!session_done) {
                status_reg = PHASE_PROBING;

                const FpgaError e = grace.build_outer_partitions(
                    rx, tx, outer_total, keytype, K, outer_slot_w, ddr2_buf,
                    session_id);
                if (e != ERR_NONE) {
                    error_code_reg = (uint32_t)e;
                    session_done = true;
                }
            }

            if (!session_done) {
                const FpgaError e = grace.join_all_partitions(
                    tx, K, outer_slot_w, ddr2_buf, ht, result_buf, matched);
                if (e != ERR_NONE) {
                    error_code_reg = (uint32_t)e;
                    session_done = true;
                }
            }
        }

        // ── Algorithm A: Linear Probing Hash Join (BRAM-only) ─────────────────
        if (!session_done && algorithm == (uint8_t)ALGORITHM_A) {

            ht.reset();
            uint32_t inner_received = 0u;
            uint16_t build_ack_pending_frames = 0u;

            // BUILD phase
            build_loop:
            for (uint32_t frame = 0u; !session_done && frame <= HT_MAX_ROWS + 1u; frame++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=96
                if (inner_received >= inner_total) break;

                uint8_t  msg_type = rx_byte_guarded(rx, tx, session_done, session_id);
                uint16_t count    = 0;
                if (!session_done) count = rx_u16(rx);
                if (session_done) break;
                const uint32_t tuple_bytes = (keytype == KEY_INT64) ? 14u : 10u;
                uint64_t build_batch_cycles = 3u + (uint64_t)count * tuple_bytes;
                timing.inner_frames++;
                timing.bytes_rx += (uint32_t)build_batch_cycles;
                timing.build_rx_cycles += build_batch_cycles;

                if (msg_type == (uint8_t)MSG_RESET) {
                    ht.reset();
                    status_reg = PHASE_IDLE;
                    tx_status_frame(tx, MSG_ACK, PHASE_IDLE, 0u, 0u, 0u, session_id);
                    session_done = true;
                    break;
                }

                if (msg_type != (uint8_t)MSG_INNER_DATA) {
                    tx_error(tx, ERR_BAD_STATE);
                    error_code_reg = ERR_BAD_STATE;
                    session_done = true;
                    break;
                }

                rx_inner_batch:
                for (uint16_t i = 0u; !session_done && i < count; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                    if (keytype == KEY_INT64) {
                        rx_key_buf[i] = rx_i64(rx);
                        rx_tid_buf[i] = rx_tid(rx);
                    } else {
                        rx_key_buf[i] = (int64_t)(int32_t)rx_u32(rx);
                        rx_tid_buf[i] = rx_tid(rx);
                    }
                }

                if (!session_done && count != 0u) {
                    emit_debug(tx, DBG_INFO, DBG_BUILD_KEY, (uint32_t)rx_key_buf[0]);
                }

                inner_batch:
                for (uint16_t i = 0u; !session_done && i < count; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                    uint32_t hash_steps = 0u;
                    const FpgaError err = ht.build(rx_key_buf[i], rx_tid_buf[i], hash_steps);
                    const uint64_t hash_cycles = (uint64_t)hash_steps * 2u;
                    timing.build_compute_cycles += hash_cycles;
                    build_batch_cycles += hash_cycles;
                    if (hash_steps > 0u) {
                        timing.hash_build_collision_steps += hash_steps - 1u;
                        if (hash_steps > timing.hash_max_build_probe_distance) {
                            timing.hash_max_build_probe_distance = (uint16_t)hash_steps;
                        }
                    }
                    if (err != ERR_NONE) {
                        timing.hash_overflow_errors++;
                        tx_error(tx, err);
                        error_code_reg = (uint32_t)err;
                        session_done = true;
                    } else {
                        timing.hash_build_inserts++;
                        inner_received++;
                    }
                }

                if (!session_done) {
                    build_ack_pending_frames++;
                    const uint32_t remaining_inner = inner_total - inner_received;
                    const uint16_t credit = (remaining_inner < RX_CREDIT_TUPLES)
                                            ? (uint16_t)remaining_inner
                                            : (uint16_t)RX_CREDIT_TUPLES;
                    const bool emit_ack =
                        (build_ack_pending_frames >= ACK_BATCH_FRAMES) ||
                        (inner_received >= inner_total);
                    if (emit_ack) {
                        timing.ack_frames++;
                        timing.bytes_tx += 15u;
                        timing.ack_emit_cycles += 15u;
                        build_batch_cycles += 15u;
                        tx_status_frame(tx, MSG_ACK, PHASE_BUILDING, inner_received,
                                        0u, credit, session_id);
                        build_ack_pending_frames = 0u;
                    }
                    if (build_batch_cycles > timing.max_build_batch_cycles) {
                        timing.max_build_batch_cycles = (uint32_t)build_batch_cycles;
                    }
                }
            }

            if (!session_done) {
                emit_debug(tx, DBG_INFO, DBG_BUILD_DONE, inner_received);
            }

            // PROBE phase
            if (!session_done) {
                status_reg = PHASE_PROBING;
                uint32_t outer_received = 0u;
                uint16_t probe_ack_pending_frames = 0u;

                probe_loop:
                for (uint32_t frame = 0u; !session_done && frame < 1000000u; frame++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=1000
                    if (outer_received >= outer_total) break;

                    uint8_t  msg_type = rx_byte_guarded(rx, tx, session_done, session_id);
                    uint16_t count    = 0;
                    if (!session_done) count = rx_u16(rx);
                    if (session_done) break;
                    const uint32_t tuple_bytes = (keytype == KEY_INT64) ? 14u : 10u;
                    uint64_t probe_batch_cycles = 3u + (uint64_t)count * tuple_bytes;
                    timing.outer_frames++;
                    timing.bytes_rx += (uint32_t)probe_batch_cycles;
                    timing.probe_rx_cycles += probe_batch_cycles;

                    if (msg_type == (uint8_t)MSG_RESET) {
                        flush_result_batch(tx, result_buf, result_buf_cnt,
                                           timing, probe_batch_cycles);
                        ht.reset();
                        status_reg = PHASE_IDLE;
                        tx_status_frame(tx, MSG_ACK, PHASE_IDLE, outer_received,
                                        matched, 0u, session_id);
                        session_done = true;
                        break;
                    }

                    if (msg_type != (uint8_t)MSG_OUTER_DATA) {
                        tx_error(tx, ERR_INVALID_CMD);
                        error_code_reg = ERR_INVALID_CMD;
                        session_done = true;
                        break;
                    }

                    rx_outer_batch:
                    for (uint16_t i = 0u; !session_done && i < count; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                        if (keytype == KEY_INT64) {
                            rx_key_buf[i] = rx_i64(rx);
                            rx_tid_buf[i] = rx_tid(rx);
                        } else {
                            rx_key_buf[i] = (int64_t)(int32_t)rx_u32(rx);
                            rx_tid_buf[i] = rx_tid(rx);
                        }
                    }

                    if (!session_done && count != 0u) {
                        emit_debug(tx, DBG_INFO, DBG_PROBE_KEY, (uint32_t)rx_key_buf[0]);
                    }

                    const bool explicit_probe_end = (count == 0u);
                    const uint32_t remaining_before_batch =
                        (outer_total > outer_received) ? outer_total - outer_received : 0u;
                    const uint16_t process_count =
                        (remaining_before_batch < (uint32_t)count)
                            ? (uint16_t)remaining_before_batch
                            : count;

                    outer_batch:
                    for (uint16_t i = 0u; !session_done && i < process_count; i++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=256
                        Tid inner_tid;
                        uint32_t hash_steps = 0u;
                        const int found = ht.probe(rx_key_buf[i], inner_tid, hash_steps);
                        const uint64_t hash_cycles = (uint64_t)hash_steps * 2u;
                        timing.probe_compute_cycles += hash_cycles;
                        probe_batch_cycles += hash_cycles;
                        timing.hash_probe_lookups++;
                        if (hash_steps > 0u) {
                            timing.hash_probe_collision_steps += hash_steps - 1u;
                            if (hash_steps > timing.hash_max_probe_distance) {
                                timing.hash_max_probe_distance = (uint16_t)hash_steps;
                            }
                        }
                        if (i == 0u) {
                            const uint32_t key_low = (uint32_t)rx_key_buf[i] & 0x7fffffffu;
                            emit_debug(tx, DBG_INFO, DBG_PROBE_HIT,
                                       (found ? 0x80000000u : 0u) | key_low);
                        }
                        if (found) {
                            result_buf[result_buf_cnt].inner_tid = inner_tid;
                            result_buf[result_buf_cnt].outer_tid = rx_tid_buf[i];
                            result_buf_cnt++;
                            matched++;
                            timing.hash_probe_hits++;
                            if (result_buf_cnt >= RESULT_FLUSH_PAIRS) {
                                flush_result_batch(tx, result_buf, result_buf_cnt,
                                                   timing, probe_batch_cycles);
                            }
                        } else {
                            timing.hash_probe_misses++;
                        }
                        outer_received++;
                    }

                    const bool probe_done = explicit_probe_end || (outer_received >= outer_total);
                    if (probe_done) {
                        flush_result_batch(tx, result_buf, result_buf_cnt,
                                           timing, probe_batch_cycles);
                    }
                    probe_ack_pending_frames++;
                    const uint32_t remaining_outer = probe_done
                        ? 0u
                        : (outer_total - outer_received);
                    const uint16_t credit = probe_done
                        ? 0u
                        : ((remaining_outer < RX_CREDIT_TUPLES)
                            ? (uint16_t)remaining_outer
                            : (uint16_t)RX_CREDIT_TUPLES);
                    if (probe_done) {
                        status_reg       = PHASE_DONE;
                        result_count_reg = matched;
                        error_code_reg   = ERR_NONE;
                        timing.inner_rows = inner_received;
                        timing.outer_rows = outer_received;
                        timing.matched_rows = matched;
                        timing.ack_frames++;
                        timing.bytes_tx += 15u + 3u + TIMING_SUMMARY_BYTES;
                        timing.ack_emit_cycles += 15u;
                        probe_batch_cycles += 15u + 3u + TIMING_SUMMARY_BYTES;
                        if (probe_batch_cycles > timing.max_probe_batch_cycles) {
                            timing.max_probe_batch_cycles = (uint32_t)probe_batch_cycles;
                        }
                        timing.build_total_cycles = timing.build_rx_cycles +
                                                     timing.build_compute_cycles;
                        timing.probe_total_cycles = timing.probe_rx_cycles +
                                                     timing.probe_compute_cycles +
                                                     timing.result_emit_cycles;
                        timing.hash_table_load_factor_ppm =
                            (uint32_t)(((uint64_t)timing.hash_build_inserts * 1000000ull) / HT_SLOTS);
                        timing.session_total_cycles = timing.config_cycles +
                                                      timing.build_total_cycles +
                                                      timing.probe_total_cycles +
                                                      timing.ack_emit_cycles +
                                                      (3u + TIMING_SUMMARY_BYTES);
                        apply_rtl_stream_timing(
                            timing, rtl_timing_valid, rtl_timing_clock_hz,
                            rtl_inner_frames, rtl_outer_frames, rtl_result_frames,
                            rtl_ack_frames, rtl_debug_frames, rtl_bytes_rx, rtl_bytes_tx,
                            rtl_cycle_counter, rtl_ts_config_first, rtl_ts_config_ack,
                            rtl_ts_build_first, rtl_ts_build_last, rtl_ts_build_ack_last,
                            rtl_ts_probe_first, rtl_ts_probe_last, rtl_ts_probe_ack_last,
                            rtl_ts_result_first, rtl_ts_result_last);
                        tx_timing_summary(tx, timing);
                        tx_status_frame(tx, MSG_STATUS, PHASE_DONE, outer_received,
                                        matched, 0u, session_id);
                        session_done = true;
                    } else {
                        const bool emit_ack =
                            probe_ack_pending_frames >= ACK_BATCH_FRAMES;
                        if (emit_ack) {
                            timing.ack_frames++;
                            timing.bytes_tx += 15u;
                            timing.ack_emit_cycles += 15u;
                            probe_batch_cycles += 15u;
                            tx_status_frame(tx, MSG_ACK, PHASE_PROBING, outer_received,
                                            matched, credit, session_id);
                            probe_ack_pending_frames = 0u;
                        }
                        if (probe_batch_cycles > timing.max_probe_batch_cycles) {
                            timing.max_probe_batch_cycles = (uint32_t)probe_batch_cycles;
                        }
                    }
                }
            }

        }  // end Algorithm A

        // ── Done — emit final STATUS, then loop back to IDLE ─────────────────
        if (!session_done) {
            emit_debug(tx, DBG_INFO, DBG_PROBE_DONE, matched);
            status_reg       = PHASE_DONE;
            result_count_reg = matched;
            error_code_reg   = ERR_NONE;
            if (algorithm == (uint8_t)ALGORITHM_A) {
                timing.matched_rows = matched;
                timing.ack_frames++;
                timing.bytes_tx += 15u + 3u + TIMING_SUMMARY_BYTES;
                timing.ack_emit_cycles += 15u;
                timing.build_total_cycles = timing.build_rx_cycles +
                                             timing.build_compute_cycles;
                timing.probe_total_cycles = timing.probe_rx_cycles +
                                             timing.probe_compute_cycles +
                                             timing.result_emit_cycles;
                timing.hash_table_load_factor_ppm =
                    (uint32_t)(((uint64_t)timing.hash_build_inserts * 1000000ull) / HT_SLOTS);
                timing.session_total_cycles = timing.config_cycles +
                                              timing.build_total_cycles +
                                              timing.probe_total_cycles +
                                              timing.ack_emit_cycles +
                                              (3u + TIMING_SUMMARY_BYTES);
                apply_rtl_stream_timing(
                    timing, rtl_timing_valid, rtl_timing_clock_hz,
                    rtl_inner_frames, rtl_outer_frames, rtl_result_frames,
                    rtl_ack_frames, rtl_debug_frames, rtl_bytes_rx, rtl_bytes_tx,
                    rtl_cycle_counter, rtl_ts_config_first, rtl_ts_config_ack,
                    rtl_ts_build_first, rtl_ts_build_last, rtl_ts_build_ack_last,
                    rtl_ts_probe_first, rtl_ts_probe_last, rtl_ts_probe_ack_last,
                    rtl_ts_result_first, rtl_ts_result_last);
                tx_timing_summary(tx, timing);
            }
            tx_status_frame(tx, MSG_STATUS, PHASE_DONE, 0u, matched, 0u, session_id);
        }

        // In C Simulation (csim) the function must return so the testbench can
        // check results.  During synthesis / co-sim the loop runs forever on hardware.
#ifndef __SYNTHESIS__
        break;
#endif

    }  // end session_loop
}
