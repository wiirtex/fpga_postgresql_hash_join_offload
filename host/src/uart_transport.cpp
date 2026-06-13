#include "uart_transport.hpp"
#include "logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

// ─── Shared helper ────────────────────────────────────────────────────────────

// Format up to 8 bytes as a hex string like "01 2C 00 ...".
static std::string hex_prefix(const uint8_t* p, size_t len) {
    char buf[32];  // 8 × "XX " = 24 chars + nul
    size_t n = std::min(len, size_t{8});
    size_t pos = 0;
    for (size_t i = 0; i < n; ++i)
        pos += static_cast<size_t>(snprintf(buf + pos, sizeof(buf) - pos, "%02X ", p[i]));
    if (pos > 0 && buf[pos - 1] == ' ') buf[--pos] = '\0';
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Windows implementation
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Helper: throw UartException with Win32 error message.
static void throw_win32(const char* ctx) {
    DWORD err = GetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, err, 0, buf, sizeof(buf), nullptr);
    // Strip trailing newline that FormatMessage appends.
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\r' || buf[n-1] == '\n')) buf[--n] = '\0';
    throw UartException(std::string(ctx) + ": " + buf);
}

UartTransport::UartTransport(const std::string& port,
                             uint32_t baud_rate,
                             uint32_t recv_timeout_ms)
    : handle_(INVALID_HANDLE_VALUE)
{
    // Windows requires "\\.\COMx" for COM ports >= COM10; accept both forms.
    std::string dev = port;
    if (dev.size() >= 3 &&
        dev[0] != '\\' &&
        (dev.substr(0, 3) == "COM" || dev.substr(0, 3) == "com")) {
        dev = "\\\\.\\" + port;
    }

    HANDLE h = CreateFileA(dev.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           0,          // exclusive access
                           nullptr,
                           OPEN_EXISTING,
                           0,          // synchronous I/O
                           nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw_win32(("UartTransport: open " + port).c_str());

    handle_ = static_cast<void*>(h);

    // ── Baud rate and framing (8N1) ───────────────────────────────────────────
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(h, &dcb))
        throw_win32("UartTransport: GetCommState");

    dcb.BaudRate = static_cast<DWORD>(baud_rate);
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    // Disable all flow control — the wire protocol implements its own credit flow.
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl  = DTR_CONTROL_DISABLE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    dcb.fOutX        = FALSE;
    dcb.fInX         = FALSE;

    if (!SetCommState(h, &dcb))
        throw_win32("UartTransport: SetCommState");

    // ── Read/write timeouts ───────────────────────────────────────────────────
    // ReadFile returns as soon as ≥1 byte arrives, or after recv_timeout_ms.
    // WriteTotalTimeout = 0 means wait until all bytes sent (synchronous).
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout         = 0;                              // not used
    to.ReadTotalTimeoutMultiplier  = 0;
    to.ReadTotalTimeoutConstant    = static_cast<DWORD>(recv_timeout_ms);
    to.WriteTotalTimeoutMultiplier = 0;
    to.WriteTotalTimeoutConstant   = 0;  // wait until all sent

    if (!SetCommTimeouts(h, &to))
        throw_win32("UartTransport: SetCommTimeouts");

    // Flush any stale bytes from previous session.
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    LOG_INFO("UartTransport", "opened %s at %u baud (recv timeout %u ms)",
             port.c_str(), baud_rate, recv_timeout_ms);
}

UartTransport::~UartTransport() {
    HANDLE h = static_cast<HANDLE>(handle_);
    if (h != INVALID_HANDLE_VALUE) {
        LOG_INFO("UartTransport", "closing port");
        CloseHandle(h);
    }
}

void UartTransport::send(const void* data, size_t len) {
    HANDLE h = static_cast<HANDLE>(handle_);
    const auto* p   = static_cast<const uint8_t*>(data);
    size_t      rem = len;
    while (rem > 0) {
        DWORD written = 0;
        if (!WriteFile(h, p, static_cast<DWORD>(rem), &written, nullptr))
            throw_win32("UartTransport: WriteFile");
        p   += written;
        rem -= written;
    }
    LOG_DEBUG("UartTransport", "send %zu bytes  %s",
              len, hex_prefix(static_cast<const uint8_t*>(data), len).c_str());
}

size_t UartTransport::recv(void* buf, size_t max_len) {
    HANDLE h = static_cast<HANDLE>(handle_);
    DWORD  n = 0;
    if (!ReadFile(h, buf, static_cast<DWORD>(max_len), &n, nullptr))
        throw_win32("UartTransport: ReadFile");
    if (n > 0)
        LOG_DEBUG("UartTransport", "recv %lu bytes  %s",
                  n, hex_prefix(static_cast<const uint8_t*>(buf), n).c_str());
    return static_cast<size_t>(n);  // 0 = timeout (no bytes within recv_timeout_ms)
}

void UartTransport::reset() {
    HANDLE h = static_cast<HANDLE>(handle_);
    LOG_DEBUG("UartTransport", "flush RX/TX buffers");
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
}

// ─────────────────────────────────────────────────────────────────────────────
// Linux / POSIX implementation
// ─────────────────────────────────────────────────────────────────────────────
#else  // !_WIN32

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

static void throw_posix(const char* ctx) {
    throw UartException(std::string(ctx) + ": " + strerror(errno));
}

// Map numeric baud rate to Bxxx constant.
static speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
    case   9600: return B9600;
    case  19200: return B19200;
    case  38400: return B38400;
    case  57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
#ifdef B460800
    case 460800: return B460800;
#endif
#ifdef B921600
    case 921600: return B921600;
#endif
    default:
        throw UartException("UartTransport: unsupported baud rate " +
                            std::to_string(baud));
    }
}

UartTransport::UartTransport(const std::string& port,
                             uint32_t baud_rate,
                             uint32_t recv_timeout_ms)
    : fd_(-1)
{
    fd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
    if (fd_ < 0)
        throw_posix(("UartTransport: open " + port).c_str());

    termios tty{};
    if (tcgetattr(fd_, &tty) != 0)
        throw_posix("UartTransport: tcgetattr");

    speed_t spd = baud_to_speed(baud_rate);
    cfsetispeed(&tty, spd);
    cfsetospeed(&tty, spd);

    cfmakeraw(&tty);            // 8N1, no flow control, raw bytes

    // VTIME: tenths of a second; clamp to [1, 255].
    // VMIN = 0: return as soon as ≥1 byte available or VTIME expires.
    uint32_t vtime_tenths = (recv_timeout_ms + 99u) / 100u;
    if (vtime_tenths == 0)  vtime_tenths = 1;
    if (vtime_tenths > 255) vtime_tenths = 255;

    tty.c_cc[VTIME] = static_cast<cc_t>(vtime_tenths);
    tty.c_cc[VMIN]  = 0;

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        throw_posix("UartTransport: tcsetattr");

    // Flush stale bytes.
    tcflush(fd_, TCIOFLUSH);

    LOG_INFO("UartTransport", "opened %s at %u baud (recv timeout %u ms)",
             port.c_str(), baud_rate, recv_timeout_ms);
}

UartTransport::~UartTransport() {
    if (fd_ >= 0) {
        LOG_INFO("UartTransport", "closing port");
        close(fd_);
    }
}

void UartTransport::send(const void* data, size_t len) {
    const auto* p   = static_cast<const uint8_t*>(data);
    size_t      rem = len;
    while (rem > 0) {
        ssize_t n = write(fd_, p, rem);
        if (n < 0) throw_posix("UartTransport: write");
        p   += static_cast<size_t>(n);
        rem -= static_cast<size_t>(n);
    }
    LOG_DEBUG("UartTransport", "send %zu bytes  %s",
              len, hex_prefix(static_cast<const uint8_t*>(data), len).c_str());
}

size_t UartTransport::recv(void* buf, size_t max_len) {
    ssize_t n = read(fd_, buf, max_len);
    if (n < 0) throw_posix("UartTransport: read");
    if (n > 0)
        LOG_DEBUG("UartTransport", "recv %zd bytes  %s",
                  n, hex_prefix(static_cast<const uint8_t*>(buf),
                                static_cast<size_t>(n)).c_str());
    return static_cast<size_t>(n);  // 0 = VTIME timeout
}

void UartTransport::reset() {
    LOG_DEBUG("UartTransport", "flush RX/TX buffers");
    tcflush(fd_, TCIOFLUSH);
}

#endif  // _WIN32
