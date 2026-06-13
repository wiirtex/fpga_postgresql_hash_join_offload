/*
 * fpga_hashjoin.c — PostgreSQL extension entry point.
 *
 * Responsibilities:
 *   - PG_MODULE_MAGIC (required by PostgreSQL to validate the .so)
 *   - _PG_init()  : register GUC parameters, install planner hook,
 *                   register Custom Scan methods
 *   - _PG_fini()  : restore planner hook (called on DROP EXTENSION / UNLOAD)
 */

#include "postgres.h"
#include "fmgr.h"
#include "nodes/extensible.h"
#include "optimizer/paths.h"
#include "utils/guc.h"

#include "fpga_internal.h"   /* fpga_planner_hook, fpga_create_custom_scan_state */

/* ── Module magic ─────────────────────────────────────────────────────────── */

PG_MODULE_MAGIC;

/* ── GUC variables ───────────────────────────────────────────────────────── */

#define FPGA_DEFAULT_ACK_WINDOW_FRAMES 15

bool        fpga_enabled         = true;
bool        fpga_simulation      = true;
char       *fpga_algorithm       = NULL;
char       *fpga_transport       = NULL;
char       *fpga_device          = NULL;   /* assigned by GUC */
int         fpga_device_baud     = 115200;
int         fpga_min_inner_rows  = 100;
int         fpga_max_inner_rows  = 12288;  /* HT_MAX_ROWS from hash_join_types.hpp */
int         fpga_warn_timeout_ms = 2000;
int         fpga_hard_timeout_ms = 30000;
int         fpga_max_batch_tuples = 118;
int         fpga_ack_window_frames = FPGA_DEFAULT_ACK_WINDOW_FRAMES;
bool        fpga_explain_verbose = false;

/* ── Hook chaining ───────────────────────────────────────────────────────── */

static set_join_pathlist_hook_type prev_join_pathlist_hook = NULL;

/* ── Custom Scan registration ────────────────────────────────────────────── */

const CustomScanMethods fpga_scan_methods = {
    "fpga_hashjoin",             /* CustomName — must match exactly everywhere */
    fpga_create_custom_scan_state
};

/* ── Planner hook wrapper (with chaining) ────────────────────────────────── */

static void
fpga_join_pathlist_hook(PlannerInfo        *root,
                        RelOptInfo         *joinrel,
                        RelOptInfo         *outerrel,
                        RelOptInfo         *innerrel,
                        JoinType            jointype,
                        JoinPathExtraData  *extra)
{
    /* Chain: always call the previous hook first */
    if (prev_join_pathlist_hook)
        prev_join_pathlist_hook(root, joinrel, outerrel, innerrel,
                                jointype, extra);

    /* Then potentially add an FPGA path */
    fpga_planner_hook(root, joinrel, outerrel, innerrel, jointype, extra);
}

/* ── _PG_init ─────────────────────────────────────────────────────────────── */

void _PG_init(void)
{
    /* ── GUC: fpga.enabled ─────────────────────────────────────────────── */
    DefineCustomBoolVariable(
        "fpga.enabled",
        "Enable FPGA Hash Join offloading.",
        NULL,
        &fpga_enabled,
        true,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.simulation ──────────────────────────────────────────── */
    DefineCustomBoolVariable(
        "fpga.simulation",
        "Use software simulation instead of real FPGA hardware.",
        "When on, SoftwareKernel is used (no physical board required).",
        &fpga_simulation,
        true,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    DefineCustomStringVariable(
        "fpga.algorithm",
        "FPGA hash join algorithm.",
        "Supported values: a, b.",
        &fpga_algorithm,
        "a",
        PGC_USERSET,
        0, NULL, NULL, NULL);

    DefineCustomStringVariable(
        "fpga.transport",
        "Hardware transport used when fpga.simulation is off.",
        "Supported values: uart, udp.",
        &fpga_transport,
        "uart",
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.device ──────────────────────────────────────────────── */
    DefineCustomStringVariable(
        "fpga.device",
        "FPGA device endpoint: UART path for uart transport, IPv4 address for udp transport.",
        NULL,
        &fpga_device,
        "/dev/ttyUSB0",
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.device_baud ─────────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.device_baud",
        "UART baud rate for FPGA communication.",
        NULL,
        &fpga_device_baud,
        115200,
        9600, 4000000,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.min_inner_rows ──────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.min_inner_rows",
        "Minimum estimated inner rows to attempt FPGA offload.",
        "Joins smaller than this threshold are executed by the CPU.",
        &fpga_min_inner_rows,
        100,
        0, INT_MAX,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.max_inner_rows ──────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.max_inner_rows",
        "Maximum inner rows supported by Algorithm A (BRAM hash table).",
        "Joins with more inner rows are not offloaded (HT_MAX_ROWS = 12288).",
        &fpga_max_inner_rows,
        12288,
        1, INT_MAX,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.warn_timeout_ms ─────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.warn_timeout_ms",
        "Log a warning if FPGA does not respond within this many milliseconds.",
        NULL,
        &fpga_warn_timeout_ms,
        2000,
        0, INT_MAX,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.hard_timeout_ms ─────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.hard_timeout_ms",
        "Abort FPGA join and raise an error after this many milliseconds.",
        NULL,
        &fpga_hard_timeout_ms,
        30000,
        1, INT_MAX,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── GUC: fpga.explain_verbose ─────────────────────────────────────── */
    DefineCustomIntVariable(
        "fpga.max_batch_tuples",
        "Maximum tuples sent in one FPGA protocol data frame.",
        "For the current UDP/RMII path this is capped at 118 to stay within the 1200-byte payload used by the adapter.",
        &fpga_max_batch_tuples,
        118,
        1, 118,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    DefineCustomIntVariable(
        "fpga.ack_window_frames",
        "Maximum number of FPGA DATA frames sent before waiting for ACK.",
        "Values above 1 enable host-side ACK windowing experiments.",
        &fpga_ack_window_frames,
        FPGA_DEFAULT_ACK_WINDOW_FRAMES,
        1, 64,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    DefineCustomBoolVariable(
        "fpga.explain_verbose",
        "Show FPGA timing and tuple counts in EXPLAIN ANALYZE output.",
        NULL,
        &fpga_explain_verbose,
        false,
        PGC_USERSET,
        0, NULL, NULL, NULL);

    /* ── Register Custom Scan methods ──────────────────────────────────── */
    RegisterCustomScanMethods(&fpga_scan_methods);

    /* ── Install planner hook (save previous for chaining) ─────────────── */
    prev_join_pathlist_hook  = set_join_pathlist_hook;
    set_join_pathlist_hook   = fpga_join_pathlist_hook;
}

/* ── _PG_fini ─────────────────────────────────────────────────────────────── */

void _PG_fini(void)
{
    /* Restore previous hook to avoid dangling pointer after UNLOAD */
    set_join_pathlist_hook = prev_join_pathlist_hook;
}
