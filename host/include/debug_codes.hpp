#pragma once
// host/include/debug_codes.hpp — FPGA-side debug event codes for MSG_DEBUG (0x09).
//
// MSG_DEBUG frame layout (10 bytes total):
//   [0x09][0x01][0x00]   header: type + count=1 (3 bytes)
//   [level:1B][code:2B LE][value:4B LE]   payload (7 bytes)

#include <cstdint>

enum class DbgLevel : uint8_t {
    DEBUG = 0,
    INFO  = 1,
    WARN  = 2,
    ERROR = 3,
};

enum class DbgCode : uint16_t {
    KERNEL_IDLE_ENTER  = 0x0001,  // value: loop iteration count (wraps at 2^32)
    KERNEL_CONFIGURE   = 0x0002,  // value: inner_count from payload
    KERNEL_BUILD_BATCH = 0x0003,  // value: batch_size received
    KERNEL_BUILD_DONE  = 0x0004,  // value: total rows inserted into hash table
    KERNEL_PROBE_BATCH = 0x0005,  // value: batch_size received
    KERNEL_PROBE_DONE  = 0x0006,  // value: total matches found
    KERNEL_RESET       = 0x0007,  // value: 0 (MSG_RESET received)
    KERNEL_BUILD_KEY   = 0x0008,  // value: first build key in the batch
    KERNEL_PROBE_KEY   = 0x0009,  // value: first probe key in the batch
    KERNEL_PROBE_HIT   = 0x000A,  // value: bit31=hit, bits30:0=first probe key
    KERNEL_ERROR       = 0x00FF,  // value: FpgaError code
};

// Human-readable name for a DbgCode (for host-side log output).
inline const char* dbg_code_name(DbgCode c) {
    switch (c) {
    case DbgCode::KERNEL_IDLE_ENTER:  return "IDLE_ENTER";
    case DbgCode::KERNEL_CONFIGURE:   return "CONFIGURE";
    case DbgCode::KERNEL_BUILD_BATCH: return "BUILD_BATCH";
    case DbgCode::KERNEL_BUILD_DONE:  return "BUILD_DONE";
    case DbgCode::KERNEL_PROBE_BATCH: return "PROBE_BATCH";
    case DbgCode::KERNEL_PROBE_DONE:  return "PROBE_DONE";
    case DbgCode::KERNEL_RESET:       return "RESET";
    case DbgCode::KERNEL_BUILD_KEY:   return "BUILD_KEY";
    case DbgCode::KERNEL_PROBE_KEY:   return "PROBE_KEY";
    case DbgCode::KERNEL_PROBE_HIT:   return "PROBE_HIT";
    case DbgCode::KERNEL_ERROR:       return "ERROR";
    default:                          return "UNKNOWN";
    }
}
