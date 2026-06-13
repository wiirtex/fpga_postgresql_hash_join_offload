#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

constexpr unsigned DEFAULT_FPGA_PORT = 50000;
constexpr unsigned DEFAULT_CLIENT_PORT = 50001;

enum MsgType : unsigned char {
    MSG_CONFIGURE  = 0x01,
    MSG_INNER_DATA = 0x02,
    MSG_OUTER_DATA = 0x03,
    MSG_RESULT     = 0x04,
    MSG_ACK        = 0x05,
    MSG_STATUS     = 0x06,
    MSG_ERROR      = 0x07,
    MSG_RESET      = 0x08,
    MSG_DEBUG      = 0x09,
};

enum Phase : unsigned char {
    PHASE_IDLE     = 0,
    PHASE_BUILDING = 1,
    PHASE_PROBING  = 2,
    PHASE_DONE     = 3,
};

#ifdef _WIN32
using socket_t = SOCKET;
constexpr socket_t invalid_socket = INVALID_SOCKET;

void close_socket(socket_t s) {
    if (s != INVALID_SOCKET) closesocket(s);
}

std::string socket_error() {
    const int err = WSAGetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, static_cast<DWORD>(err), 0, buf, sizeof(buf), nullptr);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n - 1] == '\r' || buf[n - 1] == '\n')) buf[--n] = '\0';
    return buf;
}
#else
using socket_t = int;
constexpr socket_t invalid_socket = -1;

void close_socket(socket_t s) {
    if (s >= 0) close(s);
}

std::string socket_error() {
    return std::strerror(errno);
}
#endif

bool parse_ipv4(const char* text, sockaddr_in& out, unsigned port) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(static_cast<uint16_t>(port));
#ifdef _WIN32
    const unsigned long addr = inet_addr(text);
    if (addr == INADDR_NONE && std::strcmp(text, "255.255.255.255") != 0) return false;
    out.sin_addr.s_addr = addr;
    return true;
#else
    return inet_pton(AF_INET, text, &out.sin_addr) == 1;
#endif
}

std::string addr_to_string(const sockaddr_in& addr) {
#ifdef _WIN32
    const char* ip = inet_ntoa(addr.sin_addr);
    return std::string(ip ? ip : "<invalid>") + ":" + std::to_string(ntohs(addr.sin_port));
#else
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(addr.sin_port));
#endif
}

bool same_endpoint(const sockaddr_in& a, const sockaddr_in& b) {
    return a.sin_family == b.sin_family &&
           a.sin_port == b.sin_port &&
           a.sin_addr.s_addr == b.sin_addr.s_addr;
}

uint16_t read_u16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t read_u32(const unsigned char* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

const char* msg_name(unsigned char msg) {
    switch (msg) {
    case MSG_CONFIGURE:  return "CONFIGURE";
    case MSG_INNER_DATA: return "INNER_DATA";
    case MSG_OUTER_DATA: return "OUTER_DATA";
    case MSG_RESULT:     return "RESULT";
    case MSG_ACK:        return "ACK";
    case MSG_STATUS:     return "STATUS";
    case MSG_ERROR:      return "ERROR";
    case MSG_RESET:      return "RESET";
    case MSG_DEBUG:      return "DEBUG";
    default:             return "UNKNOWN";
    }
}

const char* phase_name(unsigned char phase) {
    switch (phase) {
    case PHASE_IDLE:     return "IDLE";
    case PHASE_BUILDING: return "BUILDING";
    case PHASE_PROBING:  return "PROBING";
    case PHASE_DONE:     return "DONE";
    default:             return "UNKNOWN";
    }
}

std::string decode_protocol(const unsigned char* data, int n) {
    if (n < 3) {
        return "short";
    }

    const unsigned char msg = data[0];
    const uint16_t count = read_u16(data + 1);
    const int payload_bytes = n - 3;
    char out[256] = {};

    switch (msg) {
    case MSG_CONFIGURE:
        if (count == 1 && payload_bytes >= 12) {
            const unsigned alg = data[3];
            const unsigned key = data[4];
            const uint16_t hint = read_u16(data + 5);
            const uint32_t inner = read_u32(data + 7);
            const uint32_t outer = read_u32(data + 11);
            std::snprintf(out, sizeof(out),
                          "%s count=%u alg=%u key=%u hint=%u inner=%u outer=%u",
                          msg_name(msg), count, alg, key, hint, inner, outer);
        } else {
            std::snprintf(out, sizeof(out), "%s count=%u payload=%d", msg_name(msg), count, payload_bytes);
        }
        break;
    case MSG_INNER_DATA:
    case MSG_OUTER_DATA:
        std::snprintf(out, sizeof(out), "%s count=%u payload=%d tuple_bytes=%s",
                      msg_name(msg), count, payload_bytes,
                      payload_bytes == static_cast<int>(count) * 10 ? "10" :
                      payload_bytes == static_cast<int>(count) * 14 ? "14" : "?");
        break;
    case MSG_RESULT:
        std::snprintf(out, sizeof(out), "%s count=%u payload=%d expected=%u",
                      msg_name(msg), count, payload_bytes, static_cast<unsigned>(count) * 12u);
        break;
    case MSG_ACK:
    case MSG_STATUS:
        if (count == 1 && payload_bytes >= 12) {
            const unsigned phase = data[3];
            const uint16_t credit = read_u16(data + 5);
            const uint32_t rows = read_u32(data + 7);
            const uint32_t matched = read_u32(data + 11);
            std::snprintf(out, sizeof(out),
                          "%s phase=%s(%u) credit=%u rows=%u matched=%u",
                          msg_name(msg), phase_name(static_cast<unsigned char>(phase)), phase,
                          credit, rows, matched);
        } else {
            std::snprintf(out, sizeof(out), "%s count=%u payload=%d", msg_name(msg), count, payload_bytes);
        }
        break;
    case MSG_ERROR:
        std::snprintf(out, sizeof(out), "%s count=%u code=0x%02x", msg_name(msg), count,
                      payload_bytes > 0 ? data[3] : 0u);
        break;
    case MSG_DEBUG:
        if (count == 1 && payload_bytes >= 7) {
            const unsigned level = data[3];
            const uint16_t code = read_u16(data + 4);
            const uint32_t value = read_u32(data + 6);
            std::snprintf(out, sizeof(out), "%s level=%u code=0x%04x value=%u",
                          msg_name(msg), level, code, value);
        } else {
            std::snprintf(out, sizeof(out), "%s count=%u payload=%d", msg_name(msg), count, payload_bytes);
        }
        break;
    case MSG_RESET:
        std::snprintf(out, sizeof(out), "%s count=%u", msg_name(msg), count);
        break;
    default:
        std::snprintf(out, sizeof(out), "%s(0x%02x) count=%u payload=%d",
                      msg_name(msg), msg, count, payload_bytes);
        break;
    }

    return out;
}

void usage(const char* argv0) {
    std::printf("usage: %s [fpga_ip] [client_port] [fpga_port] [client_bind_ip] [fpga_bind_ip] [fpga_local_port]\n", argv0);
    std::printf("defaults: fpga_ip=169.254.242.60 client_port=50001 fpga_port=50000 client_bind_ip=0.0.0.0 fpga_bind_ip=169.254.242.59 fpga_local_port=50000\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help")) {
        usage(argv[0]);
        return 0;
    }

    const char* fpga_ip = argc >= 2 ? argv[1] : "169.254.242.60";
    const unsigned client_port = argc >= 3 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10)) : DEFAULT_CLIENT_PORT;
    const unsigned fpga_port = argc >= 4 ? static_cast<unsigned>(std::strtoul(argv[3], nullptr, 10)) : DEFAULT_FPGA_PORT;
    const char* client_bind_ip = argc >= 5 ? argv[4] : "0.0.0.0";
    const char* fpga_bind_ip = argc >= 6 ? argv[5] : "169.254.242.59";
    const unsigned fpga_local_port = argc >= 7 ? static_cast<unsigned>(std::strtoul(argv[6], nullptr, 10)) : DEFAULT_FPGA_PORT;

    if (client_port == 0 || client_port > 65535 ||
        fpga_port == 0 || fpga_port > 65535 ||
        fpga_local_port == 0 || fpga_local_port > 65535) {
        std::fprintf(stderr, "ports must be in range 1..65535\n");
        return 2;
    }

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    sockaddr_in fpga{};
    sockaddr_in client_bind{};
    sockaddr_in fpga_bind{};
    if (!parse_ipv4(fpga_ip, fpga, fpga_port)) {
        std::fprintf(stderr, "invalid FPGA IPv4 address: %s\n", fpga_ip);
        return 2;
    }
    if (!parse_ipv4(client_bind_ip, client_bind, client_port)) {
        std::fprintf(stderr, "invalid client bind IPv4 address: %s\n", client_bind_ip);
        return 2;
    }
    if (!parse_ipv4(fpga_bind_ip, fpga_bind, fpga_local_port)) {
        std::fprintf(stderr, "invalid FPGA-side bind IPv4 address: %s\n", fpga_bind_ip);
        return 2;
    }

    socket_t client_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (client_sock == invalid_socket) {
        std::fprintf(stderr, "client socket: %s\n", socket_error().c_str());
        return 1;
    }

    socket_t fpga_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fpga_sock == invalid_socket) {
        std::fprintf(stderr, "FPGA-side socket: %s\n", socket_error().c_str());
        close_socket(client_sock);
        return 1;
    }

    if (bind(client_sock, reinterpret_cast<sockaddr*>(&client_bind), sizeof(client_bind)) != 0) {
        std::fprintf(stderr, "bind client %s: %s\n", addr_to_string(client_bind).c_str(), socket_error().c_str());
        close_socket(client_sock);
        close_socket(fpga_sock);
        return 1;
    }
    if (bind(fpga_sock, reinterpret_cast<sockaddr*>(&fpga_bind), sizeof(fpga_bind)) != 0) {
        std::fprintf(stderr, "bind FPGA-side %s: %s\n", addr_to_string(fpga_bind).c_str(), socket_error().c_str());
        close_socket(client_sock);
        close_socket(fpga_sock);
        return 1;
    }

    std::printf("udp_proxy client side listening on %s\n", addr_to_string(client_bind).c_str());
    std::printf("udp_proxy FPGA side listening on %s\n", addr_to_string(fpga_bind).c_str());
    std::printf("forwarding to FPGA %s\n", addr_to_string(fpga).c_str());
    std::printf("point WSL/PostgreSQL fpga.device at the Windows host address, port %u\n", client_port);
    std::fflush(stdout);

    std::array<unsigned char, 65536> buf{};
    sockaddr_in last_client{};
    bool have_client = false;
    unsigned long long client_to_fpga = 0;
    unsigned long long fpga_to_client = 0;
    const char* gap_env = std::getenv("UDP_PROXY_F2C_GAP_MS");
    const unsigned fpga_to_client_gap_ms = gap_env
        ? static_cast<unsigned>(std::strtoul(gap_env, nullptr, 10))
        : 0u;

    if (fpga_to_client_gap_ms > 0) {
        std::printf("FPGA -> client pacing: %u ms after each datagram\n",
                    fpga_to_client_gap_ms);
        std::fflush(stdout);
    }

    while (true) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(client_sock, &rfds);
        FD_SET(fpga_sock, &rfds);
        socket_t max_sock = client_sock > fpga_sock ? client_sock : fpga_sock;

        const int ready = select(static_cast<int>(max_sock + 1), &rfds, nullptr, nullptr, nullptr);
#ifndef _WIN32
        if (ready < 0 && errno == EINTR) continue;
#endif
        if (ready < 0) {
            std::fprintf(stderr, "select: %s\n", socket_error().c_str());
            break;
        }

        const bool client_ready = FD_ISSET(client_sock, &rfds);
        const socket_t rx_sock = client_ready ? client_sock : fpga_sock;

        sockaddr_in src{};
        socklen_t src_len = sizeof(src);
        const int n = recvfrom(rx_sock,
                               reinterpret_cast<char*>(buf.data()),
                               static_cast<int>(buf.size()),
                               0,
                               reinterpret_cast<sockaddr*>(&src),
                               &src_len);
#ifndef _WIN32
        if (n < 0 && errno == EINTR) continue;
#endif
        if (n < 0) {
            std::fprintf(stderr, "recvfrom: %s\n", socket_error().c_str());
            break;
        }

        const bool from_client = rx_sock == client_sock;
        const sockaddr_in& dst = from_client ? fpga : last_client;
        const socket_t tx_sock = from_client ? fpga_sock : client_sock;
        if (!from_client && !have_client) {
            std::printf("drop %d bytes from FPGA: no WSL client yet\n", n);
            std::fflush(stdout);
            continue;
        }

        if (from_client) {
            if (!have_client || !same_endpoint(last_client, src)) {
                last_client = src;
                have_client = true;
                std::printf("client is now %s\n", addr_to_string(last_client).c_str());
            }
        }

        const int sent = sendto(tx_sock,
                                reinterpret_cast<const char*>(buf.data()),
                                n,
                                0,
                                reinterpret_cast<const sockaddr*>(&dst),
                                sizeof(dst));
        if (sent != n) {
            std::fprintf(stderr, "sendto %s: %s\n", addr_to_string(dst).c_str(), socket_error().c_str());
            break;
        }

        if (!from_client && fpga_to_client_gap_ms > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(fpga_to_client_gap_ms));

        if (from_client) {
            ++client_to_fpga;
            const std::string decoded = decode_protocol(buf.data(), n);
            std::printf("client -> FPGA %d bytes  totals c2f=%llu f2c=%llu  %s\n",
                        n, client_to_fpga, fpga_to_client, decoded.c_str());
        } else {
            ++fpga_to_client;
            const std::string decoded = decode_protocol(buf.data(), n);
            std::printf("FPGA -> client %d bytes  totals c2f=%llu f2c=%llu  %s\n",
                        n, client_to_fpga, fpga_to_client, decoded.c_str());
        }
        std::fflush(stdout);
    }

    close_socket(client_sock);
    close_socket(fpga_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 1;
}
