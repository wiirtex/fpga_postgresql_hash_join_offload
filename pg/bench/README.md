# Benchmark scripts

Run commands from the repository root unless noted otherwise.

## CPU baseline

```bash
BENCH_WARMUP=1 BENCH_RUNS=5 make -C pg cpu_bench CXX=/usr/bin/g++
```

## UDP FPGA path

The FPGA bitstream must already be programmed and the board must be reachable
from the host over Ethernet.

```bash
FPGA_UDP_HOST=169.254.242.60 \
BENCH_WARMUP=1 \
BENCH_RUNS=5 \
BENCH_CASES_FILE=pg/bench/cases/fpga_a_x16.txt \
make -C pg udp_bench CXX=/usr/bin/g++
```

## UART smoke/benchmark path

UART is kept as a physical verification transport, not as the production
performance path.

```bash
FPGA_DEVICE=/dev/ttyUSB1 FPGA_BAUD=115200 make -C pg uart_smoke CXX=/usr/bin/g++
```

## Summaries

```bash
python3 pg/bench/summarize_bench.py \
  pg/bench/results/cpu_bench_*.txt \
  pg/bench/results/udp_bench_*.txt \
  -o pg/bench/results/summary.md \
  --csv-output pg/bench/results/summary.csv
```

## Case files

Benchmark case definitions are stored in `pg/bench/cases/`.

