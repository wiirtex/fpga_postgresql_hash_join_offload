#pragma once
#include "transport.hpp"
#include <cstdint>
#include <stdexcept>
#include <string>

// UartTransport — ITransport implementation over a serial (UART) port.
//
// Windows: uses Win32 CreateFile / ReadFile / WriteFile / SetCommState.
// Linux:   uses POSIX termios (fd = open("/dev/ttyUSBx", ...)).
//
// recv() contract (matches ITransport):
//   - Returns up to max_len bytes; may return fewer on partial read.
//   - Returns 0 if no byte arrived within recv_timeout_ms.
//   - Throws UartException on OS-level error.
//
// Usage:
//   UartTransport uart("COM5", 115200);        // Windows
//   UartTransport uart("/dev/ttyUSB0", 115200); // Linux
//   FpgaClient client(uart, ccfg);

class UartException : public std::runtime_error {
public:
    explicit UartException(const std::string& msg) : std::runtime_error(msg) {}
};

class UartTransport final : public ITransport {
public:
    // Opens the serial port.
    // recv_timeout_ms: how long recv() waits for at least one byte before returning 0.
    explicit UartTransport(const std::string& port,
                           uint32_t baud_rate      = 115200,
                           uint32_t recv_timeout_ms = 200);

    ~UartTransport() override;

    // Non-copyable, non-movable (owns an OS handle).
    UartTransport(const UartTransport&)            = delete;
    UartTransport& operator=(const UartTransport&) = delete;

    void   send(const void* data, size_t len) override;
    size_t recv(void* buf,        size_t max_len) override;

    // Flushes OS RX/TX buffers (purges unread bytes).
    void   reset() override;

private:
#ifdef _WIN32
    void* handle_;   // HANDLE — stored as void* to avoid including <windows.h> here
#else
    int   fd_;
#endif
};
