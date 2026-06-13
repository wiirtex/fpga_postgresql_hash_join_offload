# Benchmark Summary

| Case | Pattern | Inner | Outer | Expected rows | CPU median ms | UART median ms | UDP median ms | CPU/UDP speedup | UDP/CPU | Runs | Rows | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| a_medium_low10 | low10 | 256 | 1020 | 102 | 0.355 | n/a | 15.949 | 0.02x | 44.93x | 1 | 102 | yes |
| a_medium_full | full | 256 | 1024 | 1024 | 0.387 | n/a | 16.145 | 0.02x | 41.72x | 1 | 1024 | yes |
| a_medium_half | half | 256 | 1024 | 512 | 0.312 | n/a | 15.405 | 0.02x | 49.38x | 1 | 512 | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 1024 | 0.371 | n/a | 16.969 | 0.02x | 45.74x | 1 | 1024 | yes |
| a_medium_none | none | 256 | 1024 | 0 | 0.387 | n/a | 15.723 | 0.02x | 40.63x | 1 | 0 | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 1024 | 0.368 | n/a | 16.724 | 0.02x | 45.45x | 1 | 1024 | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 4915 | 10.040 | n/a | 79.282 | 0.13x | 7.90x | 1 | 4915 | yes |
| a_bram_full | full | 12288 | 49152 | 49152 | 24.300 | n/a | 118.347 | 0.21x | 4.87x | 1 | 49152 | yes |
| a_bram_half | half | 12288 | 49152 | 24576 | 16.460 | n/a | 85.616 | 0.19x | 5.20x | 1 | 24576 | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 49152 | 13.469 | n/a | 115.494 | 0.12x | 8.57x | 1 | 49152 | yes |
| a_bram_none | none | 12288 | 49152 | 0 | 9.813 | n/a | 80.578 | 0.12x | 8.21x | 1 | 0 | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 49152 | 15.782 | n/a | 116.274 | 0.14x | 7.37x | 1 | 49152 | yes |

## Detailed Timing Statistics

| Case | Mode | Runs | Min ms | Median ms | Avg ms | Stddev ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| a_medium_low10 (low10) | cpu | 1 | 0.355 | 0.355 | 0.355 | 0.000 |
| a_medium_low10 (low10) | udp | 1 | 15.949 | 15.949 | 15.949 | 0.000 |
| a_medium_full (full) | cpu | 1 | 0.387 | 0.387 | 0.387 | 0.000 |
| a_medium_full (full) | udp | 1 | 16.145 | 16.145 | 16.145 | 0.000 |
| a_medium_half (half) | cpu | 1 | 0.312 | 0.312 | 0.312 | 0.000 |
| a_medium_half (half) | udp | 1 | 15.405 | 15.405 | 15.405 | 0.000 |
| a_medium_hotkey (hotkey) | cpu | 1 | 0.371 | 0.371 | 0.371 | 0.000 |
| a_medium_hotkey (hotkey) | udp | 1 | 16.969 | 16.969 | 16.969 | 0.000 |
| a_medium_none (none) | cpu | 1 | 0.387 | 0.387 | 0.387 | 0.000 |
| a_medium_none (none) | udp | 1 | 15.723 | 15.723 | 15.723 | 0.000 |
| a_medium_outer_skew (outer_skew) | cpu | 1 | 0.368 | 0.368 | 0.368 | 0.000 |
| a_medium_outer_skew (outer_skew) | udp | 1 | 16.724 | 16.724 | 16.724 | 0.000 |
| a_bram_low10 (low10) | cpu | 1 | 10.040 | 10.040 | 10.040 | 0.000 |
| a_bram_low10 (low10) | udp | 1 | 79.282 | 79.282 | 79.282 | 0.000 |
| a_bram_full (full) | cpu | 1 | 24.300 | 24.300 | 24.300 | 0.000 |
| a_bram_full (full) | udp | 1 | 118.347 | 118.347 | 118.347 | 0.000 |
| a_bram_half (half) | cpu | 1 | 16.460 | 16.460 | 16.460 | 0.000 |
| a_bram_half (half) | udp | 1 | 85.616 | 85.616 | 85.616 | 0.000 |
| a_bram_hotkey (hotkey) | cpu | 1 | 13.469 | 13.469 | 13.469 | 0.000 |
| a_bram_hotkey (hotkey) | udp | 1 | 115.494 | 115.494 | 115.494 | 0.000 |
| a_bram_none (none) | cpu | 1 | 9.813 | 9.813 | 9.813 | 0.000 |
| a_bram_none (none) | udp | 1 | 80.578 | 80.578 | 80.578 | 0.000 |
| a_bram_outer_skew (outer_skew) | cpu | 1 | 15.782 | 15.782 | 15.782 | 0.000 |
| a_bram_outer_skew (outer_skew) | udp | 1 | 116.274 | 116.274 | 116.274 | 0.000 |

## Source Files

- `pg/bench/results/cpu_bench_20260608_151127.txt`
- `pg/bench/results/udp_bench_20260608_151722.txt`
