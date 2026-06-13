# Benchmark Summary

| Case | Pattern | Inner | Outer | Expected rows | CPU median ms | UART median ms | UDP median ms | CPU/UDP speedup | UDP/CPU | Runs | Rows | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 102 | n/a | n/a | 15.030 | n/a | n/a | 1 | 102 | yes |
| a_medium_full | full | 256 | 1024 | 1024 | n/a | n/a | 16.559 | n/a | n/a | 1 | 1024 | yes |
| a_medium_half | half | 256 | 1024 | 512 | n/a | n/a | 15.720 | n/a | n/a | 1 | 512 | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 1024 | n/a | n/a | 15.993 | n/a | n/a | 1 | 1024 | yes |
| a_medium_none | none | 256 | 1024 | 0 | n/a | n/a | 15.518 | n/a | n/a | 1 | 0 | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 1024 | n/a | n/a | 17.321 | n/a | n/a | 1 | 1024 | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 4915 | n/a | n/a | 79.388 | n/a | n/a | 1 | 4915 | yes |
| a_bram_max_x4_low10 | low10 | 12288 | 49150 | 4915 | 10.544 | n/a | n/a | n/a | n/a | 1 | 4915 | yes |
| a_bram_full | full | 12288 | 49152 | 49152 | n/a | n/a | 125.211 | n/a | n/a | 1 | 49152 | yes |
| a_bram_half | half | 12288 | 49152 | 24576 | n/a | n/a | 83.369 | n/a | n/a | 1 | 24576 | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 49152 | n/a | n/a | 122.904 | n/a | n/a | 1 | 49152 | yes |
| a_bram_max_x4_full | full | 12288 | 49152 | 49152 | 13.827 | n/a | n/a | n/a | n/a | 1 | 49152 | yes |
| a_bram_max_x4_half | half | 12288 | 49152 | 24576 | 11.594 | n/a | n/a | n/a | n/a | 1 | 24576 | yes |
| a_bram_max_x4_hotkey | hotkey | 12288 | 49152 | 49152 | 13.558 | n/a | n/a | n/a | n/a | 1 | 49152 | yes |
| a_bram_max_x4_none | none | 12288 | 49152 | 0 | 8.754 | n/a | n/a | n/a | n/a | 1 | 0 | yes |
| a_bram_max_x4_outer_skew | outer_skew | 12288 | 49152 | 49152 | 13.767 | n/a | n/a | n/a | n/a | 1 | 49152 | yes |
| a_bram_none | none | 12288 | 49152 | 0 | n/a | n/a | 78.509 | n/a | n/a | 1 | 0 | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 49152 | n/a | n/a | 123.748 | n/a | n/a | 1 | 49152 | yes |
| a_bram_max_x16_low10 | low10 | 12288 | 196600 | 19660 | 37.475 | n/a | n/a | n/a | n/a | 1 | 19660 | yes |
| a_bram_max_x16_full | full | 12288 | 196608 | 196608 | 58.793 | n/a | n/a | n/a | n/a | 1 | 196608 | yes |
| a_bram_max_x16_half | half | 12288 | 196608 | 98304 | 52.960 | n/a | n/a | n/a | n/a | 1 | 98304 | yes |
| a_bram_max_x16_hotkey | hotkey | 12288 | 196608 | 196608 | 53.792 | n/a | n/a | n/a | n/a | 1 | 196608 | yes |
| a_bram_max_x16_none | none | 12288 | 196608 | 0 | 35.666 | n/a | n/a | n/a | n/a | 1 | 0 | yes |
| a_bram_max_x16_outer_skew | outer_skew | 12288 | 196608 | 196608 | 56.756 | n/a | n/a | n/a | n/a | 1 | 196608 | yes |

## Detailed Timing Statistics

| Case | Mode | Runs | Min ms | Median ms | Avg ms | Stddev ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| a_medium_low10 (low10) | udp | 1 | 15.030 | 15.030 | 15.030 | 0.000 |
| a_medium_full (full) | udp | 1 | 16.559 | 16.559 | 16.559 | 0.000 |
| a_medium_half (half) | udp | 1 | 15.720 | 15.720 | 15.720 | 0.000 |
| a_medium_hotkey (hotkey) | udp | 1 | 15.993 | 15.993 | 15.993 | 0.000 |
| a_medium_none (none) | udp | 1 | 15.518 | 15.518 | 15.518 | 0.000 |
| a_medium_outer_skew (outer_skew) | udp | 1 | 17.321 | 17.321 | 17.321 | 0.000 |
| a_bram_low10 (low10) | udp | 1 | 79.388 | 79.388 | 79.388 | 0.000 |
| a_bram_max_x4_low10 (low10) | cpu | 1 | 10.544 | 10.544 | 10.544 | 0.000 |
| a_bram_full (full) | udp | 1 | 125.211 | 125.211 | 125.211 | 0.000 |
| a_bram_half (half) | udp | 1 | 83.369 | 83.369 | 83.369 | 0.000 |
| a_bram_hotkey (hotkey) | udp | 1 | 122.904 | 122.904 | 122.904 | 0.000 |
| a_bram_max_x4_full (full) | cpu | 1 | 13.827 | 13.827 | 13.827 | 0.000 |
| a_bram_max_x4_half (half) | cpu | 1 | 11.594 | 11.594 | 11.594 | 0.000 |
| a_bram_max_x4_hotkey (hotkey) | cpu | 1 | 13.558 | 13.558 | 13.558 | 0.000 |
| a_bram_max_x4_none (none) | cpu | 1 | 8.754 | 8.754 | 8.754 | 0.000 |
| a_bram_max_x4_outer_skew (outer_skew) | cpu | 1 | 13.767 | 13.767 | 13.767 | 0.000 |
| a_bram_none (none) | udp | 1 | 78.509 | 78.509 | 78.509 | 0.000 |
| a_bram_outer_skew (outer_skew) | udp | 1 | 123.748 | 123.748 | 123.748 | 0.000 |
| a_bram_max_x16_low10 (low10) | cpu | 1 | 37.475 | 37.475 | 37.475 | 0.000 |
| a_bram_max_x16_full (full) | cpu | 1 | 58.793 | 58.793 | 58.793 | 0.000 |
| a_bram_max_x16_half (half) | cpu | 1 | 52.960 | 52.960 | 52.960 | 0.000 |
| a_bram_max_x16_hotkey (hotkey) | cpu | 1 | 53.792 | 53.792 | 53.792 | 0.000 |
| a_bram_max_x16_none (none) | cpu | 1 | 35.666 | 35.666 | 35.666 | 0.000 |
| a_bram_max_x16_outer_skew (outer_skew) | cpu | 1 | 56.756 | 56.756 | 56.756 | 0.000 |

## Source Files

- `pg/bench/results/cpu_bench_20260608_141408.txt`
- `pg/bench/results/udp_bench_20260608_150224.txt`
