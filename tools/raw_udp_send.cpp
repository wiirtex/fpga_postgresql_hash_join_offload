#include <winsock2.h>
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

struct pcap_addr;

struct pcap_if {
    pcap_if* next;
    char* name;
    char* description;
    pcap_addr* addresses;
    unsigned int flags;
};

using pcap_t = void;
using pcap_findalldevs_fn = int(__cdecl*)(pcap_if**, char*);
using pcap_freealldevs_fn = void(__cdecl*)(pcap_if*);
using pcap_open_live_fn = pcap_t*(__cdecl*)(const char*, int, int, int, char*);
using pcap_close_fn = void(__cdecl*)(pcap_t*);
using pcap_sendpacket_fn = int(__cdecl*)(pcap_t*, const unsigned char*, int);
struct pcap_pkthdr {
    timeval ts;
    unsigned int caplen;
    unsigned int len;
};
using pcap_next_ex_fn = int(__cdecl*)(pcap_t*, pcap_pkthdr**, const unsigned char**);
using pcap_geterr_fn = char*(__cdecl*)(pcap_t*);

static uint16_t checksum16(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        sum += (static_cast<uint16_t>(data[i]) << 8) | data[i + 1];
    }
    if (len & 1) {
        sum += static_cast<uint16_t>(data[len - 1]) << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffffu) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

static void put_be16(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v);
}

static void put_ipv4(uint8_t* p, uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    p[0] = a;
    p[1] = b;
    p[2] = c;
    p[3] = d;
}

int main(int argc, char** argv) {
    const char* match = (argc >= 2) ? argv[1] : "ASIX";
    int repeat = (argc >= 3) ? std::atoi(argv[2]) : 1;
    bool listen_only = (argc >= 2) && (std::strcmp(argv[1], "listen") == 0);
    if (listen_only) {
        match = (argc >= 3) ? argv[2] : "ASIX";
        repeat = 0;
    }
    bool broadcast_mac = (argc >= 4) && (std::strcmp(argv[3], "broadcast") == 0);
    bool listen_after_send = listen_only || ((argc >= 4) && (std::strcmp(argv[3], "listen") == 0));
    if (repeat < 1) repeat = 1;

    HMODULE wpcap = LoadLibraryA("wpcap.dll");
    if (!wpcap) {
        wpcap = LoadLibraryA("C:\\Windows\\System32\\Npcap\\wpcap.dll");
    }
    if (!wpcap) {
        std::fprintf(stderr, "failed to load wpcap.dll\n");
        return 1;
    }

    auto pcap_findalldevs = reinterpret_cast<pcap_findalldevs_fn>(GetProcAddress(wpcap, "pcap_findalldevs"));
    auto pcap_freealldevs = reinterpret_cast<pcap_freealldevs_fn>(GetProcAddress(wpcap, "pcap_freealldevs"));
    auto pcap_open_live = reinterpret_cast<pcap_open_live_fn>(GetProcAddress(wpcap, "pcap_open_live"));
    auto pcap_close = reinterpret_cast<pcap_close_fn>(GetProcAddress(wpcap, "pcap_close"));
    auto pcap_sendpacket = reinterpret_cast<pcap_sendpacket_fn>(GetProcAddress(wpcap, "pcap_sendpacket"));
    auto pcap_next_ex = reinterpret_cast<pcap_next_ex_fn>(GetProcAddress(wpcap, "pcap_next_ex"));
    auto pcap_geterr = reinterpret_cast<pcap_geterr_fn>(GetProcAddress(wpcap, "pcap_geterr"));
    if (!pcap_findalldevs || !pcap_freealldevs || !pcap_open_live || !pcap_close || !pcap_sendpacket || !pcap_next_ex || !pcap_geterr) {
        std::fprintf(stderr, "failed to resolve required pcap symbols\n");
        return 1;
    }

    char errbuf[256] = {};
    pcap_if* alldevs = nullptr;
    if (pcap_findalldevs(&alldevs, errbuf) != 0) {
        std::fprintf(stderr, "pcap_findalldevs: %s\n", errbuf);
        return 1;
    }

    pcap_if* selected = nullptr;
    for (pcap_if* d = alldevs; d; d = d->next) {
        const char* desc = d->description ? d->description : "";
        const char* name = d->name ? d->name : "";
        if (std::strstr(desc, match) || std::strstr(name, match)) {
            selected = d;
            break;
        }
    }
    if (!selected) {
        std::fprintf(stderr, "no pcap interface matched '%s'\n", match);
        for (pcap_if* d = alldevs; d; d = d->next) {
            std::fprintf(stderr, "  %s | %s\n", d->name ? d->name : "", d->description ? d->description : "");
        }
        pcap_freealldevs(alldevs);
        return 1;
    }

    std::printf("using interface: %s | %s\n",
                selected->name ? selected->name : "",
                selected->description ? selected->description : "");

    pcap_t* handle = pcap_open_live(selected->name, 65536, 0, 1000, errbuf);
    if (!handle) {
        std::fprintf(stderr, "pcap_open_live: %s\n", errbuf);
        pcap_freealldevs(alldevs);
        return 1;
    }

    uint8_t frame[60] = {};
    const uint8_t dst_mac_local[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
    const uint8_t dst_mac_bcast[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    const uint8_t src_mac[6] = {0x9c, 0x69, 0xd3, 0x1f, 0x21, 0x5e};
    std::memcpy(frame + 0, broadcast_mac ? dst_mac_bcast : dst_mac_local, 6);
    std::memcpy(frame + 6, src_mac, 6);
    put_be16(frame + 12, 0x0800);

    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    ip[1] = 0x00;
    put_be16(ip + 2, 20 + 8 + 3);
    put_be16(ip + 4, 0x1234);
    put_be16(ip + 6, 0x0000);
    ip[8] = 64;
    ip[9] = 17;
    put_be16(ip + 10, 0x0000);
    put_ipv4(ip + 12, 169, 254, 242, 59);
    put_ipv4(ip + 16, 169, 254, 242, 60);
    put_be16(ip + 10, checksum16(ip, 20));

    uint8_t* udp = ip + 20;
    put_be16(udp + 0, 50001);
    put_be16(udp + 2, 50000);
    put_be16(udp + 4, 8 + 3);
    put_be16(udp + 6, 0x0000);
    udp[8] = 0x08;
    udp[9] = 0x00;
    udp[10] = 0x00;

    int sent_count = 0;
    if (!listen_only) {
        for (int i = 0; i < repeat; ++i) {
            int rc = pcap_sendpacket(handle, frame, sizeof(frame));
            if (rc != 0) {
                std::fprintf(stderr, "pcap_sendpacket: %s\n", pcap_geterr(handle));
                pcap_close(handle);
                pcap_freealldevs(alldevs);
                return 1;
            }
            ++sent_count;
            Sleep(50);
        }
    }

    std::printf("sent %d raw Ethernet UDP RESET frame(s), %zu bytes each\n", sent_count, sizeof(frame));
    if (listen_after_send) {
        std::printf("listening for FPGA UDP replies for ~3 seconds...\n");
        DWORD deadline = GetTickCount() + 3000;
        int matched = 0;
        while (GetTickCount() < deadline) {
            pcap_pkthdr* hdr = nullptr;
            const unsigned char* pkt = nullptr;
            int rc = pcap_next_ex(handle, &hdr, &pkt);
            if (rc == 0) continue;
            if (rc < 0) {
                std::fprintf(stderr, "pcap_next_ex: %s\n", pcap_geterr(handle));
                break;
            }
            if (!hdr || !pkt || hdr->caplen < 42) continue;
            const uint8_t* p = reinterpret_cast<const uint8_t*>(pkt);
            bool from_fpga = std::memcmp(p + 6, dst_mac_local, 6) == 0;
            bool ipv4_udp = p[12] == 0x08 && p[13] == 0x00 && p[23] == 17;
            bool src_ip_fpga = p[26] == 169 && p[27] == 254 && p[28] == 242 && p[29] == 60;
            bool udp_50000 = p[34] == 0xc3 && p[35] == 0x50;
            if (from_fpga || (ipv4_udp && src_ip_fpga) || (ipv4_udp && udp_50000)) {
                std::printf("rx len=%u src=%02x:%02x:%02x:%02x:%02x:%02x dst=%02x:%02x:%02x:%02x:%02x:%02x",
                            hdr->caplen,
                            p[6], p[7], p[8], p[9], p[10], p[11],
                            p[0], p[1], p[2], p[3], p[4], p[5]);
                if (ipv4_udp && hdr->caplen >= 42) {
                    unsigned ip_header_len = (p[14] & 0x0f) * 4;
                    unsigned udp_offset = 14 + ip_header_len;
                    if (hdr->caplen >= udp_offset + 8) {
                        unsigned payload_offset = udp_offset + 8;
                        unsigned payload_len = hdr->caplen > payload_offset ? hdr->caplen - payload_offset : 0;
                        std::printf(" udp %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u payload=",
                                    p[26], p[27], p[28], p[29],
                                    (p[udp_offset] << 8) | p[udp_offset + 1],
                                    p[30], p[31], p[32], p[33],
                                    (p[udp_offset + 2] << 8) | p[udp_offset + 3]);
                        for (unsigned i = 0; i < payload_len && i < 32; ++i) {
                            std::printf("%02x", p[payload_offset + i]);
                        }
                    }
                }
                std::printf("\n");
                ++matched;
            }
        }
        std::printf("matched replies: %d\n", matched);
    }
    pcap_close(handle);
    pcap_freealldevs(alldevs);
    return 0;
}
