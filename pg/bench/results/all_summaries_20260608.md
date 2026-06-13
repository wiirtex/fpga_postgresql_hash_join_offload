# Consolidated Benchmark Summaries, 2026-06-08

This file consolidates the benchmark summaries collected on 2026-06-08 for the
post-diploma UDP transport experiments. All runs use Algorithm A unless noted
otherwise.

## Source summaries

| File | Purpose |
| --- | --- |
| `summary_ack10_final_patterns_20260608.md` | Final 70 MHz pattern baseline with `ACK_WINDOW=10`. |
| `summary_ack10_kernel90_patterns_20260608.md` | Same pattern set with 90 MHz kernel bitstream. |
| `summary_ack10_kernel100_patterns_20260608.md` | Same pattern set with 100 MHz kernel bitstream. |
| `summary_ack10_kernel100_full_timing_20260608.md` | 100 MHz size ladder, including `a_bram_max_x16 full`. |
| `summary_ack11_x4_x16_20260608.md` | Earlier `ACK_WINDOW=11` x4/x16 experiment. Kept for history. |
| `summary_ack10_x4_x16_20260608.md` | CPU-only x4/x16 summary used with direct UDP logs. Kept for history. |
| `summary_ack10_final_20260608.md` | Mixed summary from the final baseline batch. Superseded by the more specific files above. |

## Run configuration

| Parameter | Value |
| --- | --- |
| Transport | UDP/RMII, 100 Mb/s half-duplex Ethernet profile |
| FPGA algorithm | `a` / BRAM Linear Probing Hash Join |
| ACK window | `10` frames for final runs |
| Max host batch | `118` rows |
| Runs / warmup | `BENCH_RUNS=1`, `BENCH_WARMUP=0` |
| 70 MHz bitstream | `hash_join_eth100_half_phase45_rx_fifo8192_ack_batch2_no_rxdiag_rtl_20260605.bit` |
| 90 MHz bitstream | `hash_join_eth100_half_phase45_kernel90mhz_20260605_232801.bit` |
| 100 MHz bitstream | `hash_join_eth100_half_phase45_kernel100mhz_20260606_015047.bit` |

## Final 70 MHz pattern baseline

From `summary_ack10_final_patterns_20260608.md`.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 0.355 | 15.030 | 42.34x | yes |
| a_medium_full | full | 256 | 1024 | 0.387 | 16.559 | 42.79x | yes |
| a_medium_half | half | 256 | 1024 | 0.312 | 15.720 | 50.38x | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 0.371 | 15.993 | 43.11x | yes |
| a_medium_none | none | 256 | 1024 | 0.387 | 15.518 | 40.10x | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 0.368 | 17.321 | 47.07x | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 10.040 | 79.388 | 7.91x | yes |
| a_bram_full | full | 12288 | 49152 | 24.300 | 125.211 | 5.15x | yes |
| a_bram_half | half | 12288 | 49152 | 16.460 | 83.369 | 5.06x | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 13.469 | 122.904 | 9.12x | yes |
| a_bram_none | none | 12288 | 49152 | 9.813 | 78.509 | 8.00x | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 15.782 | 123.748 | 7.84x | yes |

## 90 MHz pattern run

From `summary_ack10_kernel90_patterns_20260608.md`.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 0.355 | 15.121 | 42.59x | yes |
| a_medium_full | full | 256 | 1024 | 0.387 | 18.363 | 47.45x | yes |
| a_medium_half | half | 256 | 1024 | 0.312 | 17.743 | 56.87x | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 0.371 | 16.412 | 44.24x | yes |
| a_medium_none | none | 256 | 1024 | 0.387 | 16.039 | 41.44x | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 0.368 | 16.549 | 44.97x | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 10.040 | 83.174 | 8.28x | yes |
| a_bram_full | full | 12288 | 49152 | 24.300 | 117.415 | 4.83x | yes |
| a_bram_half | half | 12288 | 49152 | 16.460 | 85.855 | 5.22x | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 13.469 | 118.389 | 8.79x | yes |
| a_bram_none | none | 12288 | 49152 | 9.813 | 76.443 | 7.79x | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 15.782 | 117.739 | 7.46x | yes |

## 100 MHz pattern run

From `summary_ack10_kernel100_patterns_20260608.md`.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 0.355 | 15.949 | 44.93x | yes |
| a_medium_full | full | 256 | 1024 | 0.387 | 16.145 | 41.72x | yes |
| a_medium_half | half | 256 | 1024 | 0.312 | 15.405 | 49.38x | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 0.371 | 16.969 | 45.74x | yes |
| a_medium_none | none | 256 | 1024 | 0.387 | 15.723 | 40.63x | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 0.368 | 16.724 | 45.45x | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 10.040 | 79.282 | 7.90x | yes |
| a_bram_full | full | 12288 | 49152 | 24.300 | 118.347 | 4.87x | yes |
| a_bram_half | half | 12288 | 49152 | 16.460 | 85.616 | 5.20x | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 13.469 | 115.494 | 8.57x | yes |
| a_bram_none | none | 12288 | 49152 | 9.813 | 80.578 | 8.21x | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 15.782 | 116.274 | 7.37x | yes |

## 100 MHz full size ladder

From `summary_ack10_kernel100_full_timing_20260608.md`.

`Board-local ms` is recomputed from `board_session_total_cycles / 100 MHz`.
The timing payload still reports `board_clock_hz=70000000`, so the raw
`FPGA time (ms)` printed by the PostgreSQL explain hook should not be used for
these 100 MHz local-board comparisons.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Board-local ms | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| tiny | full | 16 | 64 | 0.137 | 13.930 | 101.68x | 0.020 | yes |
| small | full | 64 | 256 | 0.170 | 13.957 | 82.10x | 0.072 | yes |
| medium | full | 256 | 1024 | 0.353 | 15.550 | 44.05x | 0.281 | yes |
| rx4 | full | 1024 | 4096 | 1.238 | 22.343 | 18.05x | 1.114 | yes |
| large | full | 4096 | 16384 | 4.923 | 48.667 | 9.89x | 4.449 | yes |
| a_bram_max_x4 | full | 12288 | 49152 | 15.918 | 114.926 | 7.22x | 13.419 | yes |
| a_bram_max_x16 | full | 12288 | 196608 | 60.731 | 394.414 | 6.49x | 49.163 | yes |

## 100 MHz large pattern extension

From `summary_ack10_kernel100_full_timing_20260608.md`.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| a_bram_low10 | low10 | 12288 | 49150 | 10.264 | 93.860 | 9.14x | yes |
| a_bram_half | half | 12288 | 49152 | 12.182 | 86.960 | 7.14x | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 14.054 | 113.938 | 8.11x | yes |
| a_bram_max_x4 | full | 12288 | 49152 | 15.918 | 114.926 | 7.22x | yes |
| a_bram_none | none | 12288 | 49152 | 8.538 | 78.980 | 9.25x | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 14.586 | 116.168 | 7.96x | yes |
| a_bram_max_x16 | full | 12288 | 196608 | 60.731 | 394.414 | 6.49x | yes |

## Historical ACK_WINDOW=11 x4/x16 run

From `summary_ack11_x4_x16_20260608.md`. This run is not the final baseline,
but it is useful for tracking the ACK-window experiments.

| Case | Pattern | Inner | Outer | CPU ms | UDP ms | UDP/CPU | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| a_bram_max_x4_low10 | low10 | 12288 | 49150 | 10.544 | 78.790 | 7.47x | yes |
| a_bram_max_x4_full | full | 12288 | 49152 | 13.827 | 123.816 | 8.95x | yes |
| a_bram_max_x4_half | half | 12288 | 49152 | 11.594 | 84.054 | 7.25x | yes |
| a_bram_max_x4_hotkey | hotkey | 12288 | 49152 | 13.558 | 124.683 | 9.20x | yes |
| a_bram_max_x4_none | none | 12288 | 49152 | 8.754 | 76.338 | 8.72x | yes |
| a_bram_max_x4_outer_skew | outer_skew | 12288 | 49152 | 13.767 | 125.951 | 9.15x | yes |
| a_bram_max_x16_low10 | low10 | 12288 | 196600 | 37.475 | 241.817 | 6.45x | yes |
| a_bram_max_x16_full | full | 12288 | 196608 | 58.793 | 429.455 | 7.30x | yes |
| a_bram_max_x16_half | half | 12288 | 196608 | 52.960 | 263.104 | 4.97x | yes |
| a_bram_max_x16_hotkey | hotkey | 12288 | 196608 | 53.792 | 421.345 | 7.83x | yes |
| a_bram_max_x16_none | none | 12288 | 196608 | 35.666 | 230.971 | 6.48x | yes |
| a_bram_max_x16_outer_skew | outer_skew | 12288 | 196608 | 56.756 | 428.581 | 7.55x | yes |

## Key takeaways

- All listed UDP runs passed correctness checks.
- Moving from the older 70 MHz board-local path to 90/100 MHz reduces the
  board-local cycle-derived time, but end-to-end PostgreSQL time changes only
  modestly because transport and host-side protocol costs dominate.
- The strongest defense data point is `a_bram_max_x16 full` at 100 MHz:
  PostgreSQL-over-UDP is `394.414 ms` versus CPU `60.731 ms`, but board-local
  total time is `49.163 ms`. This supports the claim that the FPGA path has
  useful local compute potential, while the current external UDP integration
  still loses end-to-end.
