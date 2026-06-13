#include "udp_transport.hpp"
#include "logger.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

static std::string hex_prefix_udp(const uint8_t* p, size_t len) {
    char buf[32];
    size_t n = std::min(len, size_t{8});
    size_t pos = 0;
    for (size_t i = 0; i < n; ++i)
        pos += static_cast<size_t>(snprintf(buf + pos, sizeof(buf) - pos, "%02X ", p[i]));
    if (pos > 0 && buf[pos - 1] == ' ') buf[--pos] = '\0';
    return buf;
}

struct UdpTransport::Impl {
#ifdef _WIN32
    SOCKET sock = INVALID_SOCKET;
    static inline int wsa_users = 0;
#else
    int sock = -1;
#endif
    sockaddr_storage remote_addr{};
    socklen_t remote_len = 0;
    uint32_t recv_timeout_ms = 200;
    size_t max_payload = 1200;
    std::vector<uint8_t> rx_packet;
    size_t rx_offset = 0;
};

#ifdef _WIN32
static void throw_socket_error(const char* ctx) {
    int err = WSAGetLastError();
    char buf[256];
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(err), 0, buf, sizeof(buf), nullptr);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';
    throw UdpException(std::string(ctx) + ": " + buf);
}

static void close_socket(SOCKET s) {
    if (s != INVALID_SOCKET) closesocket(s);
}
#else
static void throw_socket_error(const char* ctx) {
    throw UdpException(std::string(ctx) + ": " + strerror(errno));
}

static void close_socket(int s) {
    if (s >= 0) close(s);
}
#endif

static sockaddr_storage resolve_ipv4_udp(const std::string& host,
                                         uint16_t port,
                                         socklen_t& out_len) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    addrinfo* res = nullptr;
    const std::string port_s = std::to_string(port);
    const int rc = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &res);
    if (rc != 0) {
#ifdef _WIN32
        throw UdpException("UdpTransport: getaddrinfo: " + std::to_string(rc));
#else
        throw UdpException(std::string("UdpTransport: getaddrinfo: ") + gai_strerror(rc));
#endif
    }

    sockaddr_storage ss{};
    std::memcpy(&ss, res->ai_addr, res->ai_addrlen);
    out_len = static_cast<socklen_t>(res->ai_addrlen);
    freeaddrinfo(res);
    return ss;
}

static bool same_ipv4_endpoint(const sockaddr_storage& a, socklen_t a_len,
                               const sockaddr_storage& b, socklen_t b_len) {
    if (a.ss_family != AF_INET || b.ss_family != AF_INET) return false;
    if (a_len < static_cast<socklen_t>(sizeof(sockaddr_in)) ||
        b_len < static_cast<socklen_t>(sizeof(sockaddr_in))) return false;
    const auto* aa = reinterpret_cast<const sockaddr_in*>(&a);
    const auto* bb = reinterpret_cast<const sockaddr_in*>(&b);
    return aa->sin_addr.s_addr == bb->sin_addr.s_addr &&
           aa->sin_port == bb->sin_port;
}

UdpTransport::UdpTransport(const std::string& remote_host,
                           uint16_t remote_port,
                           uint16_t local_port,
                           uint32_t recv_timeout_ms,
                           size_t max_datagram_payload)
    : impl_(new Impl())
{
#ifdef _WIN32
    if (Impl::wsa_users++ == 0) {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            throw_socket_error("UdpTransport: WSAStartup");
    }
#endif

    impl_->recv_timeout_ms = recv_timeout_ms;
    impl_->max_payload = max_datagram_payload == 0 ? 1200 : max_datagram_payload;
    impl_->remote_addr = resolve_ipv4_udp(remote_host, remote_port, impl_->remote_len);

    impl_->sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (impl_->sock == INVALID_SOCKET)
#else
    if (impl_->sock < 0)
#endif
        throw_socket_error("UdpTransport: socket");

    const int recv_buf_bytes = 1 << 20;
    if (setsockopt(impl_->sock, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&recv_buf_bytes),
                   sizeof(recv_buf_bytes)) != 0) {
        LOG_WARN("UdpTransport", "failed to set SO_RCVBUF=%d", recv_buf_bytes);
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(local_port);
    if (bind(impl_->sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        close_socket(impl_->sock);
        throw_socket_error("UdpTransport: bind");
    }

    LOG_INFO("UdpTransport", "opened %s:%u from local UDP port %u (recv timeout %u ms)",
             remote_host.c_str(), remote_port, local_port, recv_timeout_ms);
}

UdpTransport::~UdpTransport() {
    if (impl_) {
        LOG_INFO("UdpTransport", "closing socket");
        close_socket(impl_->sock);
#ifdef _WIN32
        if (--Impl::wsa_users == 0) WSACleanup();
#endif
    }
}

void UdpTransport::send(const void* data, size_t len) {
    const auto* p = static_cast<const uint8_t*>(data);
    size_t rem = len;
    while (rem > 0) {
        const size_t chunk = std::min(rem, impl_->max_payload);
        const int n = sendto(impl_->sock,
                             reinterpret_cast<const char*>(p),
                             static_cast<int>(chunk),
                             0,
                             reinterpret_cast<const sockaddr*>(&impl_->remote_addr),
                             impl_->remote_len);
        if (n < 0) throw_socket_error("UdpTransport: sendto");
        if (static_cast<size_t>(n) != chunk)
            throw UdpException("UdpTransport: short UDP send");
        p += chunk;
        rem -= chunk;
    }

    LOG_DEBUG("UdpTransport", "send %zu bytes  %s",
              len, hex_prefix_udp(static_cast<const uint8_t*>(data), len).c_str());
}

size_t UdpTransport::recv(void* buf, size_t max_len) {
    auto* out = static_cast<uint8_t*>(buf);
    if (max_len == 0) return 0;

    while (impl_->rx_offset >= impl_->rx_packet.size()) {
        impl_->rx_packet.clear();
        impl_->rx_offset = 0;

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(impl_->sock, &rfds);

        timeval tv{};
        tv.tv_sec = static_cast<long>(impl_->recv_timeout_ms / 1000u);
        tv.tv_usec = static_cast<long>((impl_->recv_timeout_ms % 1000u) * 1000u);

        const int ready = select(static_cast<int>(impl_->sock + 1), &rfds, nullptr, nullptr, &tv);
#ifndef _WIN32
        if (ready < 0 && errno == EINTR) continue;
#endif
        if (ready < 0) throw_socket_error("UdpTransport: select");
        if (ready == 0) return 0;

        impl_->rx_packet.resize(65536);
        sockaddr_storage src{};
        socklen_t src_len = sizeof(src);
        const int n = recvfrom(impl_->sock,
                               reinterpret_cast<char*>(impl_->rx_packet.data()),
                               static_cast<int>(impl_->rx_packet.size()),
                               0,
                               reinterpret_cast<sockaddr*>(&src),
                               &src_len);
#ifndef _WIN32
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n < 0) throw_socket_error("UdpTransport: recvfrom");
        if (!same_ipv4_endpoint(src, src_len, impl_->remote_addr, impl_->remote_len)) {
            LOG_DEBUG("UdpTransport", "drop UDP datagram from unexpected endpoint: %d bytes", n);
            continue;
        }
        impl_->rx_packet.resize(static_cast<size_t>(n));
    }

    const size_t available = impl_->rx_packet.size() - impl_->rx_offset;
    const size_t n = std::min(max_len, available);
    std::memcpy(out, impl_->rx_packet.data() + impl_->rx_offset, n);
    impl_->rx_offset += n;
    if (impl_->rx_offset >= impl_->rx_packet.size()) {
        impl_->rx_packet.clear();
        impl_->rx_offset = 0;
    }

    LOG_DEBUG("UdpTransport", "recv %zu bytes  %s",
              n, hex_prefix_udp(out, n).c_str());
    return n;
}

void UdpTransport::reset() {
    impl_->rx_packet.clear();
    impl_->rx_offset = 0;

    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(impl_->sock, &rfds);
        timeval tv{};
        const int ready = select(static_cast<int>(impl_->sock + 1), &rfds, nullptr, nullptr, &tv);
        if (ready <= 0) break;

        uint8_t discard[2048];
        sockaddr_storage src{};
        socklen_t src_len = sizeof(src);
        const int n = recvfrom(impl_->sock,
                               reinterpret_cast<char*>(discard),
                               sizeof(discard),
                               0,
                               reinterpret_cast<sockaddr*>(&src),
                               &src_len);
        if (n <= 0) break;
        if (!same_ipv4_endpoint(src, src_len, impl_->remote_addr, impl_->remote_len)) {
            LOG_DEBUG("UdpTransport", "drop stale UDP datagram from unexpected endpoint: %d bytes", n);
        }
    }

    LOG_DEBUG("UdpTransport", "flush RX datagrams");
}

uint16_t UdpTransport::local_port() const {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(impl_->sock, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
        throw_socket_error("UdpTransport: getsockname");
    return ntohs(addr.sin_port);
}

uint32_t UdpTransport::recv_timeout_ms() const {
    return impl_->recv_timeout_ms;
}

void UdpTransport::set_recv_timeout_ms(uint32_t timeout_ms) {
    impl_->recv_timeout_ms = timeout_ms;
}

void UdpTransport::drain_until_quiet(unsigned quiet_timeouts,
                                     size_t discard_buffer_size) {
    if (quiet_timeouts == 0) return;
    if (discard_buffer_size == 0) discard_buffer_size = 2048;

    impl_->rx_packet.clear();
    impl_->rx_offset = 0;
    std::vector<uint8_t> discard(discard_buffer_size);
    for (unsigned quiet = 0; quiet < quiet_timeouts;) {
        const size_t n = recv(discard.data(), discard.size());
        if (n == 0) {
            ++quiet;
        } else {
            quiet = 0;
            LOG_DEBUG("UdpTransport", "drained stale UDP payload: %zu bytes", n);
        }
    }
}
