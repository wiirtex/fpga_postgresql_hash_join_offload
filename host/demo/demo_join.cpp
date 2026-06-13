// demo_join.cpp
//
// Demonstrates the full FPGA Hash Join pipeline using SoftwareKernel —
// a C++ simulation that speaks the real wire protocol.
//
// Shows:
//   - Input tables (employees JOIN orders)
//   - Exact hex bytes that would travel over UART to the physical FPGA
//   - Join results with matched TID pairs
//
// To run on real hardware: replace SoftwareKernel with UartTransport("/dev/ttyUSB0").
#include "software_kernel.hpp"
#include "fpga_client.hpp"
#include <cstdio>
#include <algorithm>

// ── Hex dump ─────────────────────────────────────────────────────────────────

static void hexdump(const char* label, const uint8_t* p, size_t len) {
    static const char* names[] = {
        "", "CONFIGURE", "INNER_DATA", "OUTER_DATA",
        "RESULT", "ACK", "STATUS", "ERROR", "RESET"
    };
    uint8_t msg = p[0];
    const char* name = (msg >= 1 && msg <= 8) ? names[msg] : "?";
    (void)name;  // used by label

    printf("  %-12s %3zu B   ", label, len);
    size_t show = std::min(len, (size_t)18);
    for (size_t i = 0; i < show; ++i) printf("%02X ", p[i]);
    if (len > show) printf("...");
    printf("\n");
}

// Walk the raw sent bytes and print each frame.
static void print_wire_frames(const std::vector<uint8_t>& buf, KeyType key_type) {
    size_t pos = 0;
    size_t tuple_sz = (key_type == KEY_INT32) ? 10u : 14u;
    while (pos + 3 <= buf.size()) {
        uint8_t  msg   = buf[pos];
        uint16_t count = static_cast<uint16_t>(buf[pos+1]) | (static_cast<uint16_t>(buf[pos+2]) << 8);
        size_t   payload = 0;
        const char* label = "?";
        switch (msg) {
        case MSG_CONFIGURE:  payload = 12;              label = "CONFIGURE";  break;
        case MSG_INNER_DATA: payload = count * tuple_sz; label = "INNER_DATA"; break;
        case MSG_OUTER_DATA: payload = count * tuple_sz; label = "OUTER_DATA"; break;
        case MSG_RESET:      payload = 0;               label = "RESET";      break;
        }
        hexdump(label, buf.data() + pos, 3 + payload);
        pos += 3 + payload;
    }
}

// ── main ─────────────────────────────────────────────────────────────────────

int main() {
    printf("+-------------------------------------------------+\n");
    printf("|     FPGA Hash Join  --  Wire Protocol Demo      |\n");
    printf("|  employees JOIN orders ON emp_id  (int32 keys)  |\n");
    printf("+-------------------------------------------------+\n\n");

    // Inner table: employees
    InputTuple employees[] = {
        {101, {0, 1}},
        {205, {0, 2}},
        {307, {0, 3}},
        {412, {0, 4}},
        {519, {0, 5}},
    };

    // Outer table: orders  (some emp_ids match, some don't)
    InputTuple orders[] = {
        {205, {1, 1}},   // -> emp 205
        {999, {1, 2}},   // no match
        {101, {1, 3}},   // -> emp 101
        {101, {1, 4}},   // -> emp 101  (second order)
        {777, {1, 5}},   // no match
        {519, {1, 6}},   // -> emp 519
        {412, {1, 7}},   // -> emp 412
        {888, {1, 8}},   // no match
    };
    size_t n_emp = sizeof(employees) / sizeof(employees[0]);
    size_t n_ord = sizeof(orders)    / sizeof(orders[0]);

    printf("Inner (employees): %zu rows\n", n_emp);
    for (size_t i = 0; i < n_emp; ++i)
        printf("  emp_id=%-5lld  TID=(%u,%u)\n",
               (long long)employees[i].key,
               employees[i].tid.blkno, employees[i].tid.offno);

    printf("\nOuter (orders): %zu rows\n", n_ord);
    for (size_t i = 0; i < n_ord; ++i)
        printf("  emp_id=%-5lld  TID=(%u,%u)\n",
               (long long)orders[i].key,
               orders[i].tid.blkno, orders[i].tid.offno);

    // ── Run via wire protocol ─────────────────────────────────────────────────
    SoftwareKernel kernel;

    JoinConfig cfg;
    cfg.algorithm = 0;
    cfg.key_type  = KEY_INT32;

    ClientConfig ccfg;
    ccfg.warn_timeout_ms = 100;
    ccfg.hard_timeout_ms = 1000;

    FpgaClient client(kernel, ccfg);
    auto results = client.run_hash_join(cfg, employees, n_emp, orders, n_ord);

    // ── Wire bytes ────────────────────────────────────────────────────────────
    const auto& sent = kernel.all_sent();
    printf("\nWire bytes sent to FPGA (%zu bytes total):\n", sent.size());
    printf("  (these exact bytes would travel over UART to the Nexys A7)\n");
    print_wire_frames(sent, cfg.key_type);

    // ── Results ───────────────────────────────────────────────────────────────
    printf("\nResults: %zu matches\n", results.size());
    printf("  %-25s  %-25s\n", "employee TID (inner)",    "order TID (outer)");
    printf("  %-25s  %-25s\n", "-------------------------","-------------------------");
    for (const auto& r : results)
        printf("  (blk=%u, off=%-3u)           (blk=%u, off=%-3u)\n",
               r.inner_tid.blkno, r.inner_tid.offno,
               r.outer_tid.blkno, r.outer_tid.offno);

    printf("\nTo run on the real FPGA:\n");
    printf("  Replace: SoftwareKernel kernel;\n");
    printf("  With:    UartTransport  kernel(\"/dev/ttyUSB0\", 115200);\n");
    return 0;
}
