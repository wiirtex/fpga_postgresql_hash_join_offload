cmake_minimum_required(VERSION 3.16)
project(fpga_host CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Host library ──────────────────────────────────────────────────────────────
add_library(fpga_host STATIC
    src/tuple_serializer.cpp
    src/result_decoder.cpp
    src/fpga_client.cpp
)

target_include_directories(fpga_host PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src   # hash_join_types.hpp
)

# ── Tests ─────────────────────────────────────────────────────────────────────
foreach(name test_serializer test_protocol test_client)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE fpga_host)
endforeach()

# ── Build all tests with: cmake -B build && cmake --build build ───────────────
# ── Run all tests with:   cd build && ctest --output-on-failure ──────────────
enable_testing()
add_test(NAME serializer COMMAND test_serializer)
add_test(NAME protocol   COMMAND test_protocol)
add_test(NAME client     COMMAND test_client)
