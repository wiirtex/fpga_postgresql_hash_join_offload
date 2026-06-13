## pg/Makefile — PGXS build for the fpga_hashjoin PostgreSQL extension
##
## Usage (from within pg/ in WSL):
##   make              — compile
##   make install      — install into PostgreSQL's extension directory
##   make installcheck — run pg_regress tests
##   make clean        — remove build artifacts

MODULE_big = fpga_hashjoin
EXTENSION  = fpga_hashjoin
DATA       = fpga_hashjoin--1.0.sql
PG_CFLAGS = -g -O0
PG_CPPFLAGS += -g -O0
# Uncomment to enable verbose debug logging (FPGA_LOG / FPGA_TRACE):
# PG_CPPFLAGS += -DFPGA_DEBUG


# ── Source files ──────────────────────────────────────────────────────────────
# C translation units — compiled by PGXS with standard PG CFLAGS
SRCS_C = src/fpga_hashjoin.c \
         src/fpga_planner.c  \
         src/fpga_executor.c

# C++17 translation unit — compiled by explicit rule below
SRCS_CPP = src/fpga_adapter.cpp

OBJS = $(SRCS_C:.c=.o) $(SRCS_CPP:.cpp=.o)

# ── Host library (compiled separately) ───────────────────────────────────────
# Build it first with: make host_lib  (or cmake -B ../host/build ../host)
HOST_LIB      = ../host/build/libfpga_host.a
HOST_INCLUDES = -I../host/include -I../host/demo -I../src

# Make fpga_adapter.h and host headers visible to all compilation units
# (including the .bc bitcode build for JIT that PGXS runs via clang)
PG_CPPFLAGS = -Iinclude $(HOST_INCLUDES)

# ── Extra linker flags ────────────────────────────────────────────────────────
# - HOST_LIB  : static host library (FpgaClient, TupleSerializer, etc.)
# - -lstdc++  : C++ runtime (PGXS links the final .so using the C linker)
SHLIB_LINK += $(HOST_LIB) -lstdc++

# ── Regression tests ──────────────────────────────────────────────────────────
REGRESS = fpga_hashjoin
REGRESS_OPTS = --inputdir=test

# Physical-board smoke. Keep this separate from installcheck because it needs
# a programmed board and a WSL-visible UART endpoint.
FPGA_DEVICE ?= /dev/ttyUSB1
FPGA_BAUD   ?= 115200
FPGA_UDP_HOST ?= 169.254.242.60
PSQL        ?= psql
PGDATABASE  ?= postgres

# ── PGXS boilerplate ──────────────────────────────────────────────────────────
PG_CONFIG = pg_config
PGXS      := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

# ── Explicit rule: compile the C++17 adapter translation unit ─────────────────
# Must be AFTER include $(PGXS) so that PGXS's 'all' target remains the
# default goal.  Explicit rules override PGXS's implicit %.o:%.c patterns.
src/fpga_adapter.o: src/fpga_adapter.cpp include/fpga_adapter.h
	$(CXX) -std=c++17 -g -O0 -Wall -Wextra -fPIC \
	    -Iinclude $(HOST_INCLUDES) \
	    -c $< -o $@

# ── Build the host static library ────────────────────────────────────────────
# Compiles the three host .cpp sources into a static archive used at link time.
# This rule runs automatically when libfpga_host.a is missing.
HOST_SRCS = ../host/src/logger.cpp           \
            ../host/src/tuple_serializer.cpp \
            ../host/src/result_decoder.cpp    \
            ../host/src/fpga_client.cpp       \
            ../host/src/uart_transport.cpp    \
            ../host/src/udp_transport.cpp

HOST_OBJS = $(patsubst ../host/src/%.cpp, ../host/build/%.o, $(HOST_SRCS))

../host/build/%.o: ../host/src/%.cpp
	mkdir -p ../host/build
	$(CXX) -std=c++17 -Wall -fPIC $(HOST_INCLUDES) -c $< -o $@

$(HOST_LIB): $(HOST_OBJS)
	ar rcs $@ $^

# Ensure host lib is built before linking the extension
fpga_hashjoin.so: $(HOST_LIB)

.PHONY: host_lib uart_smoke udp_smoke cpu_bench udp_bench uart_bench
host_lib: $(HOST_LIB)

uart_smoke: fpga_hashjoin.so
	PGOPTIONS=-cdynamic_library_path=$(abspath .) \
	FPGA_LOG_LEVEL=$${FPGA_LOG_LEVEL:-INFO} \
	$(PSQL) -d $(PGDATABASE) -v ON_ERROR_STOP=1 \
	    -v fpga_device="'$(FPGA_DEVICE)'" \
	    -v fpga_baud="$(FPGA_BAUD)" \
	    -f test/sql/fpga_hashjoin_uart.sql

udp_smoke: fpga_hashjoin.so
	PGOPTIONS=-cdynamic_library_path=$(abspath .) \
	FPGA_LOG_LEVEL=$${FPGA_LOG_LEVEL:-INFO} \
	$(PSQL) -d $(PGDATABASE) -v ON_ERROR_STOP=1 \
	    -v fpga_host="'$(FPGA_UDP_HOST)'" \
	    -f test/sql/fpga_hashjoin_udp.sql

cpu_bench: fpga_hashjoin.so
	PGDATABASE="$(PGDATABASE)" PSQL="$(PSQL)" \
	bash bench/run_cpu_bench.sh

udp_bench: fpga_hashjoin.so
	FPGA_UDP_HOST="$(FPGA_UDP_HOST)" \
	PGDATABASE="$(PGDATABASE)" PSQL="$(PSQL)" \
	bash bench/run_udp_bench.sh

uart_bench: fpga_hashjoin.so
	FPGA_DEVICE="$(FPGA_DEVICE)" FPGA_BAUD="$(FPGA_BAUD)" \
	PGDATABASE="$(PGDATABASE)" PSQL="$(PSQL)" \
	bash bench/run_uart_bench.sh
