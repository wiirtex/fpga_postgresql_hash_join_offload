# Benchmark Summary

| Case | Pattern | Inner | Outer | Expected rows | CPU median ms | UART median ms | UDP median ms | CPU/UDP speedup | UDP/CPU | Runs | Rows | Correct |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| tiny | full | 16 | 64 | 64 | 0.137 | n/a | 13.930 | 0.0098x | 101.68x | 1 | 64 | yes |
| small | full | 64 | 256 | 256 | 0.170 | n/a | 13.957 | 0.01x | 82.10x | 1 | 256 | yes |
| a_medium_low10 | low10 | 256 | 1020 | 102 | 0.277 | n/a | 15.373 | 0.02x | 55.50x | 1 | 102 | yes |
| a_medium_half | half | 256 | 1024 | 512 | 0.295 | n/a | 15.133 | 0.02x | 51.30x | 1 | 512 | yes |
| a_medium_hotkey | hotkey | 256 | 1024 | 1024 | 0.353 | n/a | 15.906 | 0.02x | 45.06x | 1 | 1024 | yes |
| a_medium_none | none | 256 | 1024 | 0 | 0.276 | n/a | 16.130 | 0.02x | 58.44x | 1 | 0 | yes |
| a_medium_outer_skew | outer_skew | 256 | 1024 | 1024 | 0.366 | n/a | 15.889 | 0.02x | 43.41x | 1 | 1024 | yes |
| medium | full | 256 | 1024 | 1024 | 0.353 | n/a | 15.550 | 0.02x | 44.05x | 1 | 1024 | yes |
| rx4 | full | 1024 | 4096 | 4096 | 1.238 | n/a | 22.343 | 0.06x | 18.05x | 1 | 4096 | yes |
| large | full | 4096 | 16384 | 16384 | 4.923 | n/a | 48.667 | 0.10x | 9.89x | 1 | 16384 | yes |
| a_bram_low10 | low10 | 12288 | 49150 | 4915 | 10.264 | n/a | 93.860 | 0.11x | 9.14x | 1 | 4915 | yes |
| a_bram_half | half | 12288 | 49152 | 24576 | 12.182 | n/a | 86.960 | 0.14x | 7.14x | 1 | 24576 | yes |
| a_bram_hotkey | hotkey | 12288 | 49152 | 49152 | 14.054 | n/a | 113.938 | 0.12x | 8.11x | 1 | 49152 | yes |
| a_bram_max_x4 | full | 12288 | 49152 | 49152 | 15.918 | n/a | 114.926 | 0.14x | 7.22x | 1 | 49152 | yes |
| a_bram_none | none | 12288 | 49152 | 0 | 8.538 | n/a | 78.980 | 0.11x | 9.25x | 1 | 0 | yes |
| a_bram_outer_skew | outer_skew | 12288 | 49152 | 49152 | 14.586 | n/a | 116.168 | 0.13x | 7.96x | 1 | 49152 | yes |
| a_bram_max_x16 | full | 12288 | 196608 | 196608 | 60.731 | n/a | 394.414 | 0.15x | 6.49x | 1 | 196608 | yes |

## Detailed Timing Statistics

| Case | Mode | Runs | Min ms | Median ms | Avg ms | Stddev ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| tiny (full) | cpu | 1 | 0.137 | 0.137 | 0.137 | 0.000 |
| tiny (full) | udp | 1 | 13.930 | 13.930 | 13.930 | 0.000 |
| small (full) | cpu | 1 | 0.170 | 0.170 | 0.170 | 0.000 |
| small (full) | udp | 1 | 13.957 | 13.957 | 13.957 | 0.000 |
| a_medium_low10 (low10) | cpu | 1 | 0.277 | 0.277 | 0.277 | 0.000 |
| a_medium_low10 (low10) | udp | 1 | 15.373 | 15.373 | 15.373 | 0.000 |
| a_medium_half (half) | cpu | 1 | 0.295 | 0.295 | 0.295 | 0.000 |
| a_medium_half (half) | udp | 1 | 15.133 | 15.133 | 15.133 | 0.000 |
| a_medium_hotkey (hotkey) | cpu | 1 | 0.353 | 0.353 | 0.353 | 0.000 |
| a_medium_hotkey (hotkey) | udp | 1 | 15.906 | 15.906 | 15.906 | 0.000 |
| a_medium_none (none) | cpu | 1 | 0.276 | 0.276 | 0.276 | 0.000 |
| a_medium_none (none) | udp | 1 | 16.130 | 16.130 | 16.130 | 0.000 |
| a_medium_outer_skew (outer_skew) | cpu | 1 | 0.366 | 0.366 | 0.366 | 0.000 |
| a_medium_outer_skew (outer_skew) | udp | 1 | 15.889 | 15.889 | 15.889 | 0.000 |
| medium (full) | cpu | 1 | 0.353 | 0.353 | 0.353 | 0.000 |
| medium (full) | udp | 1 | 15.550 | 15.550 | 15.550 | 0.000 |
| rx4 (full) | cpu | 1 | 1.238 | 1.238 | 1.238 | 0.000 |
| rx4 (full) | udp | 1 | 22.343 | 22.343 | 22.343 | 0.000 |
| large (full) | cpu | 1 | 4.923 | 4.923 | 4.923 | 0.000 |
| large (full) | udp | 1 | 48.667 | 48.667 | 48.667 | 0.000 |
| a_bram_low10 (low10) | cpu | 1 | 10.264 | 10.264 | 10.264 | 0.000 |
| a_bram_low10 (low10) | udp | 1 | 93.860 | 93.860 | 93.860 | 0.000 |
| a_bram_half (half) | cpu | 1 | 12.182 | 12.182 | 12.182 | 0.000 |
| a_bram_half (half) | udp | 1 | 86.960 | 86.960 | 86.960 | 0.000 |
| a_bram_hotkey (hotkey) | cpu | 1 | 14.054 | 14.054 | 14.054 | 0.000 |
| a_bram_hotkey (hotkey) | udp | 1 | 113.938 | 113.938 | 113.938 | 0.000 |
| a_bram_max_x4 (full) | cpu | 1 | 15.918 | 15.918 | 15.918 | 0.000 |
| a_bram_max_x4 (full) | udp | 1 | 114.926 | 114.926 | 114.926 | 0.000 |
| a_bram_none (none) | cpu | 1 | 8.538 | 8.538 | 8.538 | 0.000 |
| a_bram_none (none) | udp | 1 | 78.980 | 78.980 | 78.980 | 0.000 |
| a_bram_outer_skew (outer_skew) | cpu | 1 | 14.586 | 14.586 | 14.586 | 0.000 |
| a_bram_outer_skew (outer_skew) | udp | 1 | 116.168 | 116.168 | 116.168 | 0.000 |
| a_bram_max_x16 (full) | cpu | 1 | 60.731 | 60.731 | 60.731 | 0.000 |
| a_bram_max_x16 (full) | udp | 1 | 394.414 | 394.414 | 394.414 | 0.000 |

## Source Files

- `pg/bench/results/cpu_bench_20260608_152415.txt`
- `pg/bench/results/udp_bench_20260608_152426.txt`
