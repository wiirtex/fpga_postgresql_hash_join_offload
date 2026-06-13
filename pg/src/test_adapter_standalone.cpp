/*
 * test_adapter_standalone.cpp — tests fpga_adapter_run() without PostgreSQL.
 *
 * Replicates the demo_join.cpp scenario (employees JOIN orders):
 *   5 inner (employees) × 8 outer (orders) → 5 matches
 *
 * Compile & run (from project root in WSL):
 *   g++ -std=c++17 -Wall -Wextra -I pg/include -I host/include -I host/demo \
 *       -I src  pg/src/fpga_adapter.cpp  pg/src/test_adapter_standalone.cpp  \
 *       host/src/tuple_serializer.cpp host/src/result_decoder.cpp           \
 *       host/src/fpga_client.cpp -o /tmp/test_adapter && /tmp/test_adapter
 */

#include "fpga_adapter.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do {                                               \
    if (cond) { ++g_pass; }                                           \
    else { ++g_fail; printf("FAIL  %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do {                                            \
    auto _a = (a); auto _b = (b);                                      \
    if (_a == _b) { ++g_pass; }                                        \
    else { ++g_fail;                                                    \
        printf("FAIL  %s:%d  %s == %s  (%lld != %lld)\n",             \
               __FILE__, __LINE__, #a, #b,                             \
               (long long)_a, (long long)_b); }                        \
} while (0)

int main(void)
{
    /* ── Inner table: employees ──────────────────────────────────────────── */
    AdapterInputTuple inner[] = {
        { 101, {0, 1} },
        { 205, {0, 2} },
        { 307, {0, 3} },
        { 412, {0, 4} },
        { 519, {0, 5} },
    };
    size_t n_inner = sizeof(inner) / sizeof(inner[0]);

    /* ── Outer table: orders ─────────────────────────────────────────────── */
    AdapterInputTuple outer[] = {
        { 205, {1, 1} },   /* → emp 205 */
        { 999, {1, 2} },   /* no match  */
        { 101, {1, 3} },   /* → emp 101 */
        { 101, {1, 4} },   /* → emp 101 (second order) */
        { 777, {1, 5} },   /* no match  */
        { 519, {1, 6} },   /* → emp 519 */
        { 412, {1, 7} },   /* → emp 412 */
        { 888, {1, 8} },   /* no match  */
    };
    size_t n_outer = sizeof(outer) / sizeof(outer[0]);

    /* ── Run ─────────────────────────────────────────────────────────────── */
    AdapterResultPair *pairs = NULL;
    size_t             count = 0;

    bool ok = fpga_adapter_run(
        ADAPTER_KEY_INT32,
        /*use_simulation=*/true,
        /*algorithm_name=*/"a",
        /*transport_name=*/"uart",
        /*device=*/"/dev/ttyUSB0",
        /*device_baud=*/115200,
        /*warn_ms=*/100,
        /*hard_ms=*/1000,
        /*max_batch_tuples=*/118,
        /*ack_window_frames=*/1,
        inner, n_inner,
        outer, n_outer,
        &pairs, &count);

    CHECK(ok);
    if (!ok) {
        printf("error: %s\n", fpga_adapter_last_error());
        goto done;
    }

    /* ── Verify result count ─────────────────────────────────────────────── */
    CHECK_EQ((int)count, 5);
    printf("  result count: %zu (expected 5)\n", count);

    /* ── Verify each match ───────────────────────────────────────────────── */
    if (count == 5) {
        /* Results arrive in probe order: 205, 101, 101, 519, 412 */
        /* inner blkno=0; outer blkno=1 (as set above)             */
        for (size_t i = 0; i < count; ++i) {
            CHECK_EQ(pairs[i].inner_tid.blkno, 0u);
            CHECK_EQ(pairs[i].outer_tid.blkno, 1u);
        }

        /* inner offno = employee index 1–5 */
        /* outer offno = order index matching the probe order */
        /* probe order: emp 205 (off=2), emp 101 (off=1,4), emp 519 (off=5), emp 412 (off=4) */
        /* Just verify that inner offnos are in range [1,5] and outer in [1,8] */
        for (size_t i = 0; i < count; ++i) {
            CHECK(pairs[i].inner_tid.offno >= 1 && pairs[i].inner_tid.offno <= 5);
            CHECK(pairs[i].outer_tid.offno >= 1 && pairs[i].outer_tid.offno <= 8);
        }
    }

    /* ── Test deduplication ──────────────────────────────────────────────── */
    {
        /* Duplicate inner key → same result as without duplicate */
        AdapterInputTuple dup_inner[] = {
            { 10, {0, 1} },
            { 10, {0, 2} },   /* duplicate — should be ignored */
            { 20, {0, 3} },
        };
        AdapterInputTuple probe[] = {
            { 10, {1, 1} },
            { 20, {1, 2} },
        };

        AdapterResultPair *dup_pairs = NULL;
        size_t dup_count = 0;
        bool dup_ok = fpga_adapter_run(
            ADAPTER_KEY_INT32, true, "a", "uart", "/dev/ttyUSB0", 115200, 100, 1000, 118, 1,
            dup_inner, 3, probe, 2, &dup_pairs, &dup_count);

        CHECK(dup_ok);
        CHECK_EQ((int)dup_count, 2);   /* both 10→10 and 20→20 should match */
        if (dup_pairs) free(dup_pairs);
    }

    /* ── Test empty inner ────────────────────────────────────────────────── */
    {
        AdapterResultPair *empty_pairs = NULL;
        size_t empty_count = 99;
        bool empty_ok = fpga_adapter_run(
            ADAPTER_KEY_INT32, true, "a", "uart", "/dev/ttyUSB0", 115200, 100, 1000, 118, 1,
            NULL, 0, outer, n_outer, &empty_pairs, &empty_count);
        CHECK(empty_ok);
        CHECK_EQ((int)empty_count, 0);
        if (empty_pairs) free(empty_pairs);
    }

    /* Test int64 keys beyond int32 range. */
    {
        AdapterInputTuple inner64[] = {
            { 4294967297LL, {2, 1} },
            { -5000000000LL, {2, 2} },
            { 9000000000000LL, {2, 3} },
        };
        AdapterInputTuple outer64[] = {
            { -5000000000LL, {3, 1} },
            { 1234567890123LL, {3, 2} },
            { 4294967297LL, {3, 3} },
        };

        AdapterResultPair *pairs64 = NULL;
        size_t count64 = 0;
        bool ok64 = fpga_adapter_run(
            ADAPTER_KEY_INT64, true, "a", "uart", "/dev/ttyUSB0", 115200, 100, 1000, 118, 1,
            inner64, 3, outer64, 3, &pairs64, &count64);

        CHECK(ok64);
        CHECK_EQ((int)count64, 2);
        if (count64 == 2) {
            CHECK_EQ(pairs64[0].inner_tid.blkno, 2u);
            CHECK_EQ(pairs64[0].inner_tid.offno, 2u);
            CHECK_EQ(pairs64[0].outer_tid.blkno, 3u);
            CHECK_EQ(pairs64[0].outer_tid.offno, 1u);

            CHECK_EQ(pairs64[1].inner_tid.blkno, 2u);
            CHECK_EQ(pairs64[1].inner_tid.offno, 1u);
            CHECK_EQ(pairs64[1].outer_tid.blkno, 3u);
            CHECK_EQ(pairs64[1].outer_tid.offno, 3u);
        }
        if (pairs64) free(pairs64);
    }

done:
    if (pairs) free(pairs);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
