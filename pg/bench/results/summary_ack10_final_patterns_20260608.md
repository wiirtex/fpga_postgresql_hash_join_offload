# Benchmark Summary

| Case | Pattern | Inner | Outer | Expected rows | CPU median ms | UART median ms | UDP median ms | CPU/UDP speedup | UDP/CPU | Runs | Rows | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 102 | 0.355 | n/a | 15.030 | 0.02x | 42.34x | 1 | 102 | yes |
| a_medium_full | full | 256 | 1024 | 1024 | 0.387 | n/a | 16.559 | 0.02x | 42.79x | 1 | 1024 | yes |
| a_medium_half | half | 256 | 1024 | 512 | 0.312 | n/a | 15.720 | 0.02x | 50.38x | 1 | 512 | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 1024 | 0.371 | n/a | 15.993 | 0.02x | 43.11x | 1 | 1024 | yes |
| a_medium_none | none | 256 | 1024 | 0 | 0.387 | n/a | 15.518 | 0.02x | 40.10x | 1 | 0 | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 1024 | 0.368 | n/a | 17.321 | 0.02x | 47.07x | 1 | 1024 | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 4915 | 10.040 | n/a | 79.388 | 0.13x | 7.91x | 1 | 4915 | yes |
| a_bram_full | full | 12288 | 49152 | 49152 | 24.300 | n/a | 125.211 | 0.19x | 5.15x | 1 | 49152 | yes |
| a_bram_half | half | 12288 | 49152 | 24576 | 16.460 | n/a | 83.369 | 0.20x | 5.06x | 1 | 24576 | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 49152 | 13.469 | n/a | 122.904 | 0.11x | 9.12x | 1 | 49152 | yes |
| a_bram_none | none | 12288 | 49152 | 0 | 9.813 | n/a | 78.509 | 0.12x | 8.00x | 1 | 0 | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 49152 | 15.782 | n/a | 123.748 | 0.13x | 7.84x | 1 | 49152 | yes |

## Detailed Timing Statistics

| Case | Mode | Runs | Min ms | Median ms | Avg ms | Stddev ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| a_medium_low10 (low10) | cpu | 1 | 0.355 | 0.355 | 0.355 | 0.000 |
| a_medium_low10 (low10) | udp | 1 | 15.030 | 15.030 | 15.030 | 0.000 |
| a_medium_full (full) | cpu | 1 | 0.387 | 0.387 | 0.387 | 0.000 |
| a_medium_full (full) | udp | 1 | 16.559 | 16.559 | 16.559 | 0.000 |
| a_medium_half (half) | cpu | 1 | 0.312 | 0.312 | 0.312 | 0.000 |
| a_medium_half (half) | udp | 1 | 15.720 | 15.720 | 15.720 | 0.000 |
| a_medium_hotkey (hotkey) | cpu | 1 | 0.371 | 0.371 | 0.371 | 0.000 |
| a_medium_hotkey (hotkey) | udp | 1 | 15.993 | 15.993 | 15.993 | 0.000 |
| a_medium_none (none) | cpu | 1 | 0.387 | 0.387 | 0.387 | 0.000 |
| a_medium_none (none) | udp | 1 | 15.518 | 15.518 | 15.518 | 0.000 |
| a_medium_outer_skew (outer_skew) | cpu | 1 | 0.368 | 0.368 | 0.368 | 0.000 |
| a_medium_outer_skew (outer_skew) | udp | 1 | 17.321 | 17.321 | 17.321 | 0.000 |
| a_bram_low10 (low10) | cpu | 1 | 10.040 | 10.040 | 10.040 | 0.000 |
| a_bram_low10 (low10) | udp | 1 | 79.388 | 79.388 | 79.388 | 0.000 |
| a_bram_full (full) | cpu | 1 | 24.300 | 24.300 | 24.300 | 0.000 |
| a_bram_full (full) | udp | 1 | 125.211 | 125.211 | 125.211 | 0.000 |
| a_bram_half (half) | cpu | 1 | 16.460 | 16.460 | 16.460 | 0.000 |
| a_bram_half (half) | udp | 1 | 83.369 | 83.369 | 83.369 | 0.000 |
| a_bram_hotkey (hotkey) | cpu | 1 | 13.469 | 13.469 | 13.469 | 0.000 |
| a_bram_hotkey (hotkey) | udp | 1 | 122.904 | 122.904 | 122.904 | 0.000 |
| a_bram_none (none) | cpu | 1 | 9.813 | 9.813 | 9.813 | 0.000 |
| a_bram_none (none) | udp | 1 | 78.509 | 78.509 | 78.509 | 0.000 |
| a_bram_outer_skew (outer_skew) | cpu | 1 | 15.782 | 15.782 | 15.782 | 0.000 |
| a_bram_outer_skew (outer_skew) | udp | 1 | 123.748 | 123.748 | 123.748 | 0.000 |

## Source Files

- `pg/bench/results/cpu_bench_20260608_151127.txt`
- `pg/bench/results/udp_bench_20260608_150224.txt`
