# FPGA PostgreSQL Hash Join Offload

Prototype implementation of Hash Join offload from PostgreSQL to an external
FPGA board. The repository contains the HLS kernel, RTL transport/protocol
logic, host-side C++ library, PostgreSQL extension, tests, benchmark scripts,
and Vivado reconstruction flow.

The current target board is Digilent Nexys A7-100T with the LAN8720A Ethernet
PHY. UART support is kept as a physical verification path; the practical
transport path is Ethernet/UDP over RMII.

## Repository Layout

- `src/` - Vitis HLS Hash Join kernel and HLS testbench.
- `rtl/` - Verilog Ethernet/UART transport, protocol timing, debug, and helper
  modules.
- `fpga/vivado/` - Vivado block-design export and project reconstruction
  script.
- `host/` - C++17 host library, serializers, protocol client, and transports.
- `pg/` - PostgreSQL extension, SQL tests, and benchmark scripts.
- `tools/` - small helper tools used by the root `Makefile`.
- `scripts/` - minimal scripts required by the public build flow.
- `help/` - board constraint file for Nexys A7.

Generated HLS projects, Vivado projects, bitstreams, object files, logs, and
local run outputs are intentionally not committed.

## Requirements

Verified toolchain:

- AMD Vitis HLS 2025.2.
- AMD Vivado 2025.2.
- Digilent Nexys A7-100T board.
- PostgreSQL development environment with `pg_config`.
- C++17 compiler, `make`, `bash`, and Python 3.

The host/PostgreSQL flow was used from WSL. Vivado/Vitis commands below are
shown for Windows PowerShell.

## Host Tests

Run from the repository root:

```bash
make clean
make test CXX=/usr/bin/g++
make clean
```

This builds and runs deterministic host-side unit tests for serialization,
protocol decoding, and the FPGA client state machine.

## HLS IP Rebuild

Run from the repository root:

```powershell
& "D:\HLS\2025.2\Vitis\bin\vitis-run.bat" --mode hls --tcl scripts\hls_rebuild.tcl
```

The script runs C simulation, HLS synthesis, and exports the IP catalog entry
to:

```text
firstlich/hls/impl/ip
```

`firstlich/` is generated output and is ignored by Git.

## Vivado Project Rebuild

First rebuild the HLS IP. Then run:

```powershell
$env:VIVADO_BUILD_DIR="D:\tmp\fpga_pg_vivado"
& "D:\HLS\2025.2\Vivado\bin\vivado.bat" -mode batch -source fpga\vivado\rebuild_project.tcl
```

The short `VIVADO_BUILD_DIR` is intentional. Generated Vivado/HLS file names can
exceed the Windows 260-character path limit when the project is built from a
deep checkout path.

With the command above, the Vivado project is created under:

```text
D:\tmp\fpga_pg_vivado\hash_join_vivado
```

To also run implementation and write a bitstream:

```powershell
$env:BUILD_BITSTREAM="1"
$env:VIVADO_JOBS="4"
$env:VIVADO_BUILD_DIR="D:\tmp\fpga_pg_vivado"
& "D:\HLS\2025.2\Vivado\bin\vivado.bat" -mode batch -source fpga\vivado\rebuild_project.tcl
```

## PostgreSQL Extension

Build from the repository root:

```bash
make -C pg CXX=/usr/bin/g++
```

Install into the PostgreSQL extension directory:

```bash
make -C pg install CXX=/usr/bin/g++
```

Run SQL regression tests:

```bash
make -C pg installcheck CXX=/usr/bin/g++
```

Board-dependent smoke tests are explicit opt-in targets.

UDP smoke:

```bash
FPGA_UDP_HOST=169.254.242.60 make -C pg udp_smoke CXX=/usr/bin/g++
```

UART smoke:

```bash
FPGA_DEVICE=/dev/ttyUSB1 FPGA_BAUD=115200 make -C pg uart_smoke CXX=/usr/bin/g++
```

## Benchmarks

Benchmark scripts and case files are under `pg/bench/`.

CPU baseline:

```bash
BENCH_WARMUP=1 BENCH_RUNS=5 make -C pg cpu_bench CXX=/usr/bin/g++
```

UDP FPGA path:

```bash
FPGA_UDP_HOST=169.254.242.60 \
BENCH_WARMUP=1 \
BENCH_RUNS=5 \
BENCH_CASES_FILE=pg/bench/cases/fpga_a_x16.txt \
make -C pg udp_bench CXX=/usr/bin/g++
```

See `pg/bench/README.md` for summary generation.

## Limitations

- The implementation targets a resource-constrained Nexys A7 board, not a
  PCIe, HBM, or SoC-class accelerator.
- Algorithm A, the BRAM-based Linear Probing Hash Join, is the main physically
  evaluated UDP path.
- Algorithm B, the DDR2-based Grace Hash Join, is included in the HLS design
  and simulation flow, but the UDP physical path is not treated as the final
  stable performance path.
- UART is useful for physical verification and debugging, but it is not a
  practical database transport.
- End-to-end PostgreSQL performance is dominated by host, protocol, and network
  overheads on this external-board setup.

## License

This project is licensed under the Apache License 2.0. See `LICENSE`.

Third-party notices are listed in `THIRD_PARTY_NOTICES.md`. In particular,
`help/Nexys-A7-100T-Master.xdc` is based on Digilent's `digilent-xdc`
repository and remains available under its original MIT license.

## Verified Command Sequence

The staged repository was checked with:

```bash
make clean
make test CXX=/usr/bin/g++
make clean
```

```powershell
& "D:\HLS\2025.2\Vitis\bin\vitis-run.bat" --mode hls --tcl scripts\hls_rebuild.tcl
```

```powershell
$env:VIVADO_BUILD_DIR="D:\tmp\fpga_pg_vivado"
& "D:\HLS\2025.2\Vivado\bin\vivado.bat" -mode batch -source fpga\vivado\rebuild_project.tcl
```
