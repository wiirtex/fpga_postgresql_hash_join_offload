#pragma once
#include "transport.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

// UdpTransport - ITransport implementation over UDP datagrams.
//
// The FPGA wire protocol is byte-stream oriented.  This transport preserves the
// ITransport contract by chunking outgoing bytes into UDP payloads and exposing
// received UDP payloads as a FIFO of bytes to ResultDecoder/FpgaClient.
//
// UDP does not guarantee delivery or ordering.  For the intended direct-link
// FPGA MVP this is acceptable as a first transport step; protocol-level sequence
// numbers/retries can be added later if needed.
//
// The current FPGA TX RTL replies to a fixed host UDP port, so the default
// local bind port is FPGA_UDP_PORT. Tests or localhost tools that need an
// ephemeral port should pass local_port = 0 explicitly.

class UdpException : public std::runtime_error {
public:
    explicit UdpException(const std::string& msg) : std::runtime_error(msg) {}
};

inline constexpr uint16_t FPGA_UDP_PORT = 50000;

class UdpTransport final : public ITransport {
public:
    explicit UdpTransport(const std::string& remote_host,
                          uint16_t remote_port,
                          uint16_t local_port = FPGA_UDP_PORT,
                          uint32_t recv_timeout_ms = 200,
                          size_t max_datagram_payload = 1200);

    ~UdpTransport() override;

    UdpTransport(const UdpTransport&)            = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    void   send(const void* data, size_t len) override;
    size_t recv(void* buf,        size_t max_len) override;
    void   reset() override;

    uint16_t local_port() const;
    uint32_t recv_timeout_ms() const;
    void set_recv_timeout_ms(uint32_t timeout_ms);
    void drain_until_quiet(unsigned quiet_timeouts = 6,
                           size_t discard_buffer_size = 2048);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
