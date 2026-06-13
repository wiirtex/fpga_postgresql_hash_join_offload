SHELL    := /bin/bash
CXX      := /d/c/bin/g++
CXXFLAGS := -std=c++17 -Wall -Wextra -I host/include -I src

HOST_SRC := host/src/logger.cpp            \
            host/src/tuple_serializer.cpp  \
            host/src/result_decoder.cpp    \
            host/src/fpga_client.cpp       \
            host/src/uart_transport.cpp    \
            host/src/udp_transport.cpp

EXEEXT :=
ifeq ($(OS),Windows_NT)
LDLIBS += -lws2_32
EXEEXT := .exe
endif

.PHONY: all test demo uart_smoke udp_smoke udp_proxy smoke clean

all: test

host/build:
	mkdir -p host/build

host/build/test_serializer$(EXEEXT): host/tests/test_serializer.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

host/build/test_protocol$(EXEEXT): host/tests/test_protocol.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

host/build/test_client$(EXEEXT): host/tests/test_client.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

test: host/build/test_serializer$(EXEEXT) host/build/test_protocol$(EXEEXT) host/build/test_client$(EXEEXT)
	host/build/test_serializer$(EXEEXT)
	host/build/test_protocol$(EXEEXT)
	host/build/test_client$(EXEEXT)

host/build/demo_join$(EXEEXT): host/demo/demo_join.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) -I host/demo $(HOST_SRC) $< -o $@ $(LDLIBS)

demo: host/build/demo_join$(EXEEXT)
	host/build/demo_join$(EXEEXT)

host/build/uart_smoke$(EXEEXT): host/tests/uart_smoke.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

uart_smoke: host/build/uart_smoke$(EXEEXT)

host/build/udp_smoke$(EXEEXT): host/tests/udp_smoke.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

UDP_HOST ?= 169.254.242.60

udp_smoke: host/build/udp_smoke$(EXEEXT)
	@echo "Running UDP smoke against $(UDP_HOST):50000..."
	host/build/udp_smoke$(EXEEXT) "$(UDP_HOST)"

host/build/udp_case$(EXEEXT): host/tests/udp_case.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

host/build/udp_burst_case$(EXEEXT): host/tests/udp_burst_case.cpp $(HOST_SRC) | host/build
	$(CXX) $(CXXFLAGS) $(HOST_SRC) $< -o $@ $(LDLIBS)

host/build/udp_proxy$(EXEEXT): tools/udp_proxy.cpp | host/build
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

udp_proxy: host/build/udp_proxy$(EXEEXT)
	host/build/udp_proxy$(EXEEXT) "$(UDP_HOST)"

PORT ?= $(shell powershell -ExecutionPolicy Bypass -File scripts/find_port.ps1 2>/dev/null | tr -d '\r')

smoke: host/build/uart_smoke$(EXEEXT)
	@if [ -z "$(PORT)" ]; then \
	  echo "ERROR: CP210x port not found. Usage: make smoke PORT=COM5"; \
	  exit 1; \
	fi
	@echo "Running smoke tests on $(PORT)..."
	host/build/uart_smoke$(EXEEXT) "$(PORT)"

clean:
	rm -rf host/build
