/*
 * fpga_executor.c — Custom Scan executor for FPGA Hash Join.
 *
 * Lifecycle:
 *   CreateCustomScanState  — allocate FpgaCustomScanState
 *   BeginCustomScan        — drain inner + outer sub-plans, run FPGA join
 *   ExecCustomScan         — iterate over result pairs, fetch rows by TID
 *   EndCustomScan          — close relations, free sub-plan state
 *   ReScanCustomScan       — not supported (raises ERROR)
 *   ExplainCustomScan      — print timing info (optional, when explain_verbose)
 */

#include "postgres.h"

#include "access/table.h"
#include "access/tableam.h"
#include "commands/explain.h"
#include "executor/executor.h"
#include "executor/nodeCustom.h"
#include "nodes/execnodes.h"
#include "nodes/extensible.h"
#include "nodes/pg_list.h"
#include "optimizer/optimizer.h"
#include "parser/parsetree.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/snapmgr.h"

#include <stdlib.h>     /* free() for malloc'd adapter result */
#include <time.h>       /* clock_gettime */

#include "fpga_adapter.h"
#include "fpga_internal.h"   /* fpga_create_custom_scan_state prototype */

/* GUC variables declared in fpga_hashjoin.c */
extern bool  fpga_simulation;
extern char *fpga_algorithm;
extern char *fpga_transport;
extern char *fpga_device;
extern int   fpga_device_baud;
extern int   fpga_warn_timeout_ms;
extern int   fpga_hard_timeout_ms;
extern int   fpga_max_batch_tuples;
extern int   fpga_ack_window_frames;
extern bool  fpga_explain_verbose;

/* ── State struct ─────────────────────────────────────────────────────────── */

typedef struct FpgaCustomScanState
{
    CustomScanState     css;                /* MUST be first */

    /* Sub-plan states (inner = build, outer = probe) */
    PlanState          *inner_plan_state;
    PlanState          *outer_plan_state;

    /* Heap relations for TID-based row fetch */
    Relation            inner_rel;
    Relation            outer_rel;

    /* Join key info (from custom_private in the plan node) */
    AttrNumber          inner_key_attno;    /* 1-based */
    AttrNumber          outer_key_attno;
    AdapterKeyType      key_type;

    /* FPGA results */
    AdapterResultPair  *result_pairs;       /* palloc'd */
    size_t              result_count;
    size_t              result_cursor;

    /* Slots for TID-based heap fetch */
    TupleTableSlot     *inner_fetch_slot;
    TupleTableSlot     *outer_fetch_slot;
    Snapshot            snapshot;

    /* RTI of each sub-plan's base relation (for scan-slot column routing) */
    Index               inner_rtindex;
    Index               outer_rtindex;

    /* Statistics for EXPLAIN ANALYZE */
    double              fpga_total_ms;
    double              inner_drain_ms;
    double              outer_drain_ms;
    double              adapter_run_ms;
    double              result_copy_ms;
    double              tid_fetch_ms;
    size_t              inner_tuple_count;
    size_t              outer_tuple_count;
    bool                has_adapter_metrics;
    AdapterHostMetrics  adapter_metrics;
} FpgaCustomScanState;

/* ── Forward declarations ─────────────────────────────────────────────────── */

static void begin_custom_scan(CustomScanState *node, EState *estate, int eflags);
static TupleTableSlot *exec_custom_scan(CustomScanState *node);
static void end_custom_scan(CustomScanState *node);
static void rescan_custom_scan(CustomScanState *node);
static void explain_custom_scan(CustomScanState *node, List *ancestors,
                                 ExplainState *es);
static double elapsed_ms(const struct timespec *start,
                         const struct timespec *end);

/* ── Exec methods table ───────────────────────────────────────────────────── */

const CustomExecMethods fpga_exec_methods = {
    "fpga_hashjoin",
    begin_custom_scan,
    exec_custom_scan,
    end_custom_scan,
    rescan_custom_scan,
    NULL,               /* MarkPosCustomScan */
    NULL,               /* RestrPosCustomScan */
    NULL,               /* EstimateDSMCustomScan */
    NULL,               /* InitializeDSMCustomScan */
    NULL,               /* ReInitializeDSMCustomScan */
    NULL,               /* InitializeWorkerCustomScan */
    NULL,               /* ShutdownCustomScan */
    explain_custom_scan
};

/* ── CreateCustomScanState ────────────────────────────────────────────────── */

/* Declared extern in fpga_hashjoin.c; must not be static */
Node *
fpga_create_custom_scan_state(CustomScan *cscan)
{
    FpgaCustomScanState *state;

    FPGA_LOG("fpga_hashjoin: fpga_create_custom_scan_state entered, cscan=%p custom_plans=%p len=%d",
             (void *) cscan, (void *) cscan->custom_plans,
             list_length(cscan->custom_plans));

    state = (FpgaCustomScanState *)
        newNode(sizeof(FpgaCustomScanState), T_CustomScanState);
    state->css.methods = &fpga_exec_methods;

    /* Decode custom_private: [key_type, inner_attno, outer_attno] */
    state->key_type        = (AdapterKeyType) linitial_int(cscan->custom_private);
    state->inner_key_attno = (AttrNumber)     lsecond_int(cscan->custom_private);
    state->outer_key_attno = (AttrNumber)     lthird_int(cscan->custom_private);

    FPGA_LOG("fpga_hashjoin: fpga_create_custom_scan_state done, key_type=%d inner_attno=%d outer_attno=%d",
             (int) state->key_type, (int) state->inner_key_attno, (int) state->outer_key_attno);

    return (Node *) state;
}

/* ── Helpers ──────────────────────────────────────────────────────────────── */

/* Drain a plan node into a dynamically-grown AdapterInputTuple array.
 * Returns a palloc'd array; sets *out_count.
 * NULL keys (isnull) are silently skipped.
 */
static AdapterInputTuple *
drain_plan(PlanState *plan_state, AttrNumber key_attno,
           AdapterKeyType key_type, size_t *out_count)
{
    size_t              capacity = 64;
    size_t              count    = 0;
    AdapterInputTuple  *tuples   = palloc(capacity * sizeof(AdapterInputTuple));
    TupleTableSlot     *slot;

    for (;;)
    {
        bool            isnull;
        Datum           key_datum;
        int64_t         key_val;
        TupleTableSlot *scan_slot;

        slot = ExecProcNode(plan_state);
        if (TupIsNull(slot))
            break;

        /*
         * If the SeqScan applies a projection, ExecProcNode returns a
         * VirtualTupleTableSlot whose tts_tid is InvalidBlockNumber and whose
         * attributes are indexed by targetlist position, not by varattno.
         *
         * ss_ScanTupleSlot is the raw BufferHeapTupleTableSlot filled by the
         * heap AM before projection.  It always has a valid tts_tid and the
         * original heap attribute numbering.  Use it for both key extraction
         * and TID capture.
         */
        scan_slot = ((ScanState *) plan_state)->ss_ScanTupleSlot;

        key_datum = slot_getattr(scan_slot, key_attno, &isnull);
        if (isnull)
            continue;   /* skip NULL join keys */

        if (key_type == ADAPTER_KEY_INT32)
            key_val = (int64_t) DatumGetInt32(key_datum);
        else
            key_val = (int64_t) DatumGetInt64(key_datum);

        /* Grow buffer if needed */
        if (count == capacity)
        {
            capacity *= 2;
            tuples = repalloc(tuples, capacity * sizeof(AdapterInputTuple));
        }

        tuples[count].key       = key_val;
        tuples[count].tid.blkno = ItemPointerGetBlockNumber(&scan_slot->tts_tid);
        tuples[count].tid.offno = ItemPointerGetOffsetNumber(&scan_slot->tts_tid);
        count++;
    }

    *out_count = count;
    return tuples;
}

static double
elapsed_ms(const struct timespec *start, const struct timespec *end)
{
    return (end->tv_sec  - start->tv_sec)  * 1000.0 +
           (end->tv_nsec - start->tv_nsec) / 1.0e6;
}

/* ── BeginCustomScan ──────────────────────────────────────────────────────── */

static void
begin_custom_scan(CustomScanState *node, EState *estate, int eflags)
{
    FpgaCustomScanState *state  = (FpgaCustomScanState *) node;
    CustomScan          *cscan  = (CustomScan *) node->ss.ps.plan;
    Plan                *inner_plan, *outer_plan;
    RangeTblEntry       *inner_rte, *outer_rte;
    Index                inner_rtindex, outer_rtindex;
    AdapterInputTuple   *inner_arr = NULL;
    AdapterInputTuple   *outer_arr = NULL;
    AdapterResultPair   *tmp_pairs = NULL;
    size_t               tmp_count = 0;
    struct timespec      t_start, t_end;
    bool                 ok;

    FPGA_LOG("fpga_hashjoin: begin_custom_scan entered, eflags=0x%x explain_only=%d",
             eflags, (eflags & EXEC_FLAG_EXPLAIN_ONLY) != 0);

    /*
     * EXPLAIN (without ANALYZE) calls BeginCustomScan with EXEC_FLAG_EXPLAIN_ONLY.
     * Sub-plans are initialised in stub mode — ExecProcNode on them will crash.
     * Skip actual data processing; result_count stays 0, ExecCustomScan returns NULL.
     */
    if (eflags & EXEC_FLAG_EXPLAIN_ONLY)
    {
        FPGA_LOG("fpga_hashjoin: explain-only mode, skipping FPGA execution");
        return;
    }

    /*
     * The planner stored [outer_path, inner_path] in custom_plans
     * (see fpga_planner.c: list_make2(outerrel->..., innerrel->...)).
     *
     * ExecInitCustomScan already called ExecInitNode on each custom_plans
     * entry and stored the resulting PlanState pointers in node->custom_ps.
     * We must NOT call ExecInitNode again — doing so would double-initialise
     * the sub-plans and corrupt executor state.
     */
    FPGA_LOG("fpga_hashjoin: custom_plans len=%d custom_ps len=%d",
             list_length(cscan->custom_plans), list_length(node->custom_ps));

    if (list_length(cscan->custom_plans) != 2)
        ereport(ERROR, (errmsg("fpga_hashjoin: expected 2 custom_plans, got %d",
                               list_length(cscan->custom_plans))));

    outer_plan = (Plan *) linitial(cscan->custom_plans);
    inner_plan = (Plan *) lsecond(cscan->custom_plans);

    if (list_length(node->custom_ps) == 2)
    {
        /* ExecInitCustomScan already initialised sub-plans for us */
        state->outer_plan_state = (PlanState *) linitial(node->custom_ps);
        state->inner_plan_state = (PlanState *) lsecond(node->custom_ps);
        FPGA_LOG("fpga_hashjoin: using PG-initialised sub-plans outer=%p inner=%p",
                 (void *) state->outer_plan_state, (void *) state->inner_plan_state);
    }
    else
    {
        /* PG did not initialise sub-plans (older build?) — do it ourselves */
        FPGA_LOG("fpga_hashjoin: custom_ps empty, calling ExecInitNode ourselves");
        state->outer_plan_state = ExecInitNode(outer_plan, estate, eflags);
        state->inner_plan_state = ExecInitNode(inner_plan, estate, eflags);
        FPGA_LOG("fpga_hashjoin: ExecInitNode done outer=%p inner=%p",
                 (void *) state->outer_plan_state, (void *) state->inner_plan_state);
        /*
         * Register in custom_ps so end_custom_scan can close the same child
         * plan states as the PG-initialised path.
         */
        node->custom_ps = lappend(node->custom_ps, state->outer_plan_state);
        node->custom_ps = lappend(node->custom_ps, state->inner_plan_state);
    }

    inner_rtindex = ((Scan *) inner_plan)->scanrelid;
    outer_rtindex = ((Scan *) outer_plan)->scanrelid;
    state->inner_rtindex = inner_rtindex;
    state->outer_rtindex = outer_rtindex;
    FPGA_LOG("fpga_hashjoin: rtindex inner=%u outer=%u", inner_rtindex, outer_rtindex);

    inner_rte = rt_fetch(inner_rtindex, estate->es_range_table);
    outer_rte = rt_fetch(outer_rtindex, estate->es_range_table);

    state->inner_rel = table_open(inner_rte->relid, AccessShareLock);
    state->outer_rel = table_open(outer_rte->relid, AccessShareLock);

    state->inner_fetch_slot = table_slot_create(state->inner_rel,
                                                 &estate->es_tupleTable);
    state->outer_fetch_slot = table_slot_create(state->outer_rel,
                                                 &estate->es_tupleTable);

    state->snapshot = GetActiveSnapshot();

    FPGA_LOG("fpga_hashjoin: draining inner plan, key_attno=%d", (int) state->inner_key_attno);
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    inner_arr = drain_plan(state->inner_plan_state,
                           state->inner_key_attno,
                           state->key_type,
                           &state->inner_tuple_count);
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    state->inner_drain_ms = elapsed_ms(&t_start, &t_end);
    FPGA_LOG("fpga_hashjoin: inner drained, count=%zu", state->inner_tuple_count);

    FPGA_LOG("fpga_hashjoin: draining outer plan, key_attno=%d", (int) state->outer_key_attno);
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    outer_arr = drain_plan(state->outer_plan_state,
                           state->outer_key_attno,
                           state->key_type,
                           &state->outer_tuple_count);
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    state->outer_drain_ms = elapsed_ms(&t_start, &t_end);
    FPGA_LOG("fpga_hashjoin: outer drained, count=%zu", state->outer_tuple_count);

    FPGA_LOG("fpga_hashjoin: calling fpga_adapter_run");
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    ok = fpga_adapter_run(state->key_type,
                          fpga_simulation,
                          fpga_algorithm,
                          fpga_transport,
                          fpga_device,
                          (uint32_t) fpga_device_baud,
                          (uint32_t) fpga_warn_timeout_ms,
                          (uint32_t) fpga_hard_timeout_ms,
                          (uint16_t) fpga_max_batch_tuples,
                          (uint16_t) fpga_ack_window_frames,
                          inner_arr, state->inner_tuple_count,
                          outer_arr, state->outer_tuple_count,
                          &tmp_pairs, &tmp_count);

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    state->adapter_run_ms = elapsed_ms(&t_start, &t_end);
    state->fpga_total_ms = state->adapter_run_ms;
    state->has_adapter_metrics =
        fpga_adapter_last_host_metrics(&state->adapter_metrics);

    /* Free temporary input arrays — no longer needed */
    pfree(inner_arr);
    pfree(outer_arr);

    if (!ok)
        ereport(ERROR,
                (errcode(ERRCODE_EXTERNAL_ROUTINE_EXCEPTION),
                 errmsg("FPGA hash join failed: %s",
                        fpga_adapter_last_error()),
                 errhint("Disable FPGA offloading with: SET fpga.enabled = off")));

    /* ── Copy malloc'd results into PG memory, then free the malloc ptr ── */
    clock_gettime(CLOCK_MONOTONIC, &t_start);
    state->result_count = tmp_count;
    if (tmp_count > 0)
    {
        state->result_pairs = (AdapterResultPair *)
            palloc(tmp_count * sizeof(AdapterResultPair));
        memcpy(state->result_pairs, tmp_pairs,
               tmp_count * sizeof(AdapterResultPair));
    }
    else
        state->result_pairs = NULL;

    free(tmp_pairs);    /* free the malloc'd pointer from fpga_adapter_run */
    tmp_pairs = NULL;
    clock_gettime(CLOCK_MONOTONIC, &t_end);
    state->result_copy_ms = elapsed_ms(&t_start, &t_end);

    state->result_cursor = 0;

    FPGA_LOG("fpga_hashjoin: begin done — inner=%zu outer=%zu results=%zu",
             state->inner_tuple_count, state->outer_tuple_count, state->result_count);
}

/* ── ExecCustomScan ───────────────────────────────────────────────────────── */

static TupleTableSlot *
exec_custom_scan(CustomScanState *node)
{
    FpgaCustomScanState *state = (FpgaCustomScanState *) node;
    ItemPointerData      tid;

    for (;;)
    {
        AdapterResultPair *pair;
        bool               inner_found, outer_found;

        if (state->result_cursor >= state->result_count)
        {
            FPGA_LOG("fpga_hashjoin: exec done, returned %zu rows", state->result_cursor);
            return NULL;   /* end of results */
        }

        pair = &state->result_pairs[state->result_cursor++];

        FPGA_LOG("fpga_hashjoin: exec pair %zu — inner(%u,%u) outer(%u,%u)",
             state->result_cursor - 1,
             pair->inner_tid.blkno, pair->inner_tid.offno,
             pair->outer_tid.blkno, pair->outer_tid.offno);

        {
            struct timespec t_fetch_start, t_fetch_end;

            clock_gettime(CLOCK_MONOTONIC, &t_fetch_start);

            /* Fetch inner tuple by TID */
            ItemPointerSet(&tid, pair->inner_tid.blkno, pair->inner_tid.offno);
            inner_found = table_tuple_fetch_row_version(state->inner_rel,
                                                        &tid,
                                                        state->snapshot,
                                                        state->inner_fetch_slot);

            /* Fetch outer tuple by TID */
            ItemPointerSet(&tid, pair->outer_tid.blkno, pair->outer_tid.offno);
            outer_found = table_tuple_fetch_row_version(state->outer_rel,
                                                        &tid,
                                                        state->snapshot,
                                                        state->outer_fetch_slot);

            clock_gettime(CLOCK_MONOTONIC, &t_fetch_end);
            state->tid_fetch_ms += elapsed_ms(&t_fetch_start, &t_fetch_end);
        }

        FPGA_LOG("fpga_hashjoin: inner_found=%d outer_found=%d", inner_found, outer_found);

        if (!inner_found || !outer_found)
            continue;   /* MVCC: tuple deleted between scan and fetch — skip */

        /*
         * Fill ss_ScanTupleSlot from the fetched inner/outer tuples.
         *
         * custom_scan_tlist holds the original Vars (real varno = RTI).
         * For each entry we pick the source slot (inner or outer) based on
         * varno, then extract the attribute into the corresponding column of
         * the virtual scan slot.  ExecProject reads INDEX_VAR(col) from that
         * slot via the ProjectionInfo built by ExecInitCustomScan.
         */
        {
            TupleTableSlot *scan_slot = node->ss.ss_ScanTupleSlot;
            CustomScan     *cscan     = (CustomScan *) node->ss.ps.plan;
            ListCell       *lc;
            int             col       = 0;

            ExecClearTuple(scan_slot);
            foreach(lc, cscan->custom_scan_tlist)
            {
                TargetEntry    *tle    = (TargetEntry *) lfirst(lc);
                Var            *var    = (Var *) tle->expr;
                TupleTableSlot *src    = (var->varno == state->outer_rtindex)
                                             ? state->outer_fetch_slot
                                             : state->inner_fetch_slot;
                bool            isnull;

                scan_slot->tts_values[col] =
                    slot_getattr(src, var->varattno, &isnull);
                scan_slot->tts_isnull[col] = isnull;
                col++;
            }
            ExecStoreVirtualTuple(scan_slot);

            /*
             * If ExecConditionalAssignProjectionInfo determined the targetlist
             * is a trivial identity (INDEX_VAR(1), INDEX_VAR(2), ...), it sets
             * ps_ProjInfo = NULL.  In that case the scan slot IS the result.
             */
            if (node->ss.ps.ps_ProjInfo != NULL)
                return ExecProject(node->ss.ps.ps_ProjInfo);
            return scan_slot;
        }
    }
}

/* ── EndCustomScan ────────────────────────────────────────────────────────── */

static void
end_custom_scan(CustomScanState *node)
{
    FpgaCustomScanState *state = (FpgaCustomScanState *) node;

    /*
     * The custom scan fully drains both child scans in BeginCustomScan.  End
     * them explicitly here so their scan relations are released before the
     * statement resource owner checks for leaked relcache references.
     */
    if (node->custom_ps)
    {
        ListCell *lc;

        foreach(lc, node->custom_ps)
            ExecEndNode((PlanState *) lfirst(lc));

        node->custom_ps = NIL;
    }

    /*
     * inner_rel/outer_rel are NULL when begin was skipped (EXEC_FLAG_EXPLAIN_ONLY).
     * Clear the fetch slots first to release any buffer pins before closing
     * the underlying relations (prevents stale pin/relation state).
     * Null the pointers after close to guard against accidental double-close.
     */
    if (state->inner_fetch_slot)
        ExecClearTuple(state->inner_fetch_slot);
    if (state->outer_fetch_slot)
        ExecClearTuple(state->outer_fetch_slot);

    if (state->inner_rel)
    {
        table_close(state->inner_rel, AccessShareLock);
        state->inner_rel = NULL;
    }
    if (state->outer_rel)
    {
        table_close(state->outer_rel, AccessShareLock);
        state->outer_rel = NULL;
    }

    /* result_pairs is palloc'd → freed with memory context; no explicit pfree needed */
    /* fetch slots are registered in es_tupleTable → freed by PG EndExecutor */
}

/* ── ReScanCustomScan ─────────────────────────────────────────────────────── */

static void
rescan_custom_scan(CustomScanState *node)
{
    (void) node;
    ereport(ERROR,
            (errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
             errmsg("FPGA hash join does not support rescan"),
             errhint("This join appears inside a loop — disable FPGA offload: "
                     "SET fpga.enabled = off")));
}

/* ── ExplainCustomScan ────────────────────────────────────────────────────── */

static void
explain_custom_scan(CustomScanState *node, List *ancestors, ExplainState *es)
{
    FpgaCustomScanState *state = (FpgaCustomScanState *) node;

    (void) ancestors;

    FPGA_LOG("fpga_hashjoin: explain_custom_scan entered, verbose=%d, es=%p, state=%p",
         fpga_explain_verbose, (void *) es, (void *) state);

    if (fpga_explain_verbose)
    {
        const double total_custom_scan_ms =
            state->inner_drain_ms +
            state->outer_drain_ms +
            state->adapter_run_ms +
            state->result_copy_ms +
            state->tid_fetch_ms;

        FPGA_LOG("fpga_hashjoin: explain verbose path");
        ExplainPropertyFloat("FPGA time (ms)", "ms", state->fpga_total_ms, 3, es);
        ExplainPropertyFloat("Inner drain time (ms)", "ms",
                             state->inner_drain_ms, 3, es);
        ExplainPropertyFloat("Outer drain time (ms)", "ms",
                             state->outer_drain_ms, 3, es);
        ExplainPropertyFloat("Adapter run time (ms)", "ms",
                             state->adapter_run_ms, 3, es);
        ExplainPropertyFloat("Result copy time (ms)", "ms",
                             state->result_copy_ms, 3, es);
        ExplainPropertyFloat("TID fetch time (ms)", "ms",
                             state->tid_fetch_ms, 3, es);
        ExplainPropertyFloat("Custom scan measured time (ms)", "ms",
                             total_custom_scan_ms, 3, es);
        FPGA_LOG("fpga_hashjoin: after ExplainPropertyFloat");
        ExplainPropertyInteger("Inner tuples sent", NULL,
                               (int64) state->inner_tuple_count, es);
        ExplainPropertyInteger("Outer tuples sent", NULL,
                               (int64) state->outer_tuple_count, es);
        ExplainPropertyInteger("Result pairs", NULL,
                               (int64) state->result_count, es);
        if (state->has_adapter_metrics)
        {
            AdapterHostMetrics *m = &state->adapter_metrics;

            ExplainPropertyFloat("Host config send time (ms)", "ms",
                                 m->config_send_ms, 3, es);
            ExplainPropertyFloat("Host config ACK wait time (ms)", "ms",
                                 m->config_ack_wait_ms, 3, es);
            ExplainPropertyFloat("Host build send time (ms)", "ms",
                                 m->build_send_ms, 3, es);
            ExplainPropertyFloat("Host build ACK wait time (ms)", "ms",
                                 m->build_ack_wait_ms, 3, es);
            ExplainPropertyFloat("Host probe send time (ms)", "ms",
                                 m->probe_send_ms, 3, es);
            ExplainPropertyFloat("Host probe ACK wait time (ms)", "ms",
                                 m->probe_ack_wait_ms, 3, es);
            ExplainPropertyFloat("Host final STATUS wait time (ms)", "ms",
                                 m->final_status_wait_ms, 3, es);
            ExplainPropertyFloat("Host reset wait time (ms)", "ms",
                                 m->reset_wait_ms, 3, es);
            ExplainPropertyFloat("Host result receive time (ms)", "ms",
                                 m->result_recv_ms, 3, es);
            ExplainPropertyInteger("Host protocol frames sent", NULL,
                                   (int64) m->protocol_frames_sent, es);
            ExplainPropertyInteger("Host protocol frames received", NULL,
                                   (int64) m->protocol_frames_recv, es);
            ExplainPropertyInteger("Host transport sends", NULL,
                                   (int64) m->transport_sends, es);
            ExplainPropertyInteger("Host bytes sent", NULL,
                                   (int64) m->bytes_sent, es);
            ExplainPropertyInteger("Host bytes received", NULL,
                                   (int64) m->bytes_recv, es);
            ExplainPropertyInteger("Host inner frames sent", NULL,
                                   (int64) m->inner_frames_sent, es);
            ExplainPropertyInteger("Host outer frames sent", NULL,
                                   (int64) m->outer_frames_sent, es);
            ExplainPropertyInteger("Host ACK frames received", NULL,
                                   (int64) m->ack_frames_recv, es);
            ExplainPropertyInteger("Host STATUS frames received", NULL,
                                   (int64) m->status_frames_recv, es);
            ExplainPropertyInteger("Host RESULT frames received", NULL,
                                   (int64) m->result_frames_recv, es);
            ExplainPropertyInteger("Host DEBUG frames received", NULL,
                                   (int64) m->debug_frames_recv, es);
            ExplainPropertyInteger("Host TIMING frames received", NULL,
                                   (int64) m->timing_frames_recv, es);
            ExplainPropertyInteger("Host result pairs received", NULL,
                                   (int64) m->result_pairs_recv, es);
            ExplainPropertyBool("Board timing present", m->has_board_timing, es);
            if (m->has_board_timing)
            {
                ExplainPropertyInteger("Board timing version", NULL,
                                       (int64) m->board_timing_version, es);
                ExplainPropertyInteger("Board timing flags", NULL,
                                       (int64) m->board_timing_flags, es);
                ExplainPropertyInteger("Board clock Hz", NULL,
                                       (int64) m->board_clock_hz, es);
                ExplainPropertyInteger("Board inner rows", NULL,
                                       (int64) m->board_inner_rows, es);
                ExplainPropertyInteger("Board outer rows", NULL,
                                       (int64) m->board_outer_rows, es);
                ExplainPropertyInteger("Board matched rows", NULL,
                                       (int64) m->board_matched_rows, es);
                ExplainPropertyInteger("Board inner frames", NULL,
                                       (int64) m->board_inner_frames, es);
                ExplainPropertyInteger("Board outer frames", NULL,
                                       (int64) m->board_outer_frames, es);
                ExplainPropertyInteger("Board result frames", NULL,
                                       (int64) m->board_result_frames, es);
                ExplainPropertyInteger("Board ACK frames", NULL,
                                       (int64) m->board_ack_frames, es);
                ExplainPropertyInteger("Board DEBUG frames", NULL,
                                       (int64) m->board_debug_frames, es);
                ExplainPropertyInteger("Board bytes RX", NULL,
                                       (int64) m->board_bytes_rx, es);
                ExplainPropertyInteger("Board bytes TX", NULL,
                                       (int64) m->board_bytes_tx, es);
                ExplainPropertyInteger("Board session total cycles", NULL,
                                       (int64) m->board_session_total_cycles, es);
                ExplainPropertyInteger("Board config cycles", NULL,
                                       (int64) m->board_config_cycles, es);
                ExplainPropertyInteger("Board build RX cycles", NULL,
                                       (int64) m->board_build_rx_cycles, es);
                ExplainPropertyInteger("Board build compute cycles", NULL,
                                       (int64) m->board_build_compute_cycles, es);
                ExplainPropertyInteger("Board build total cycles", NULL,
                                       (int64) m->board_build_total_cycles, es);
                ExplainPropertyInteger("Board probe RX cycles", NULL,
                                       (int64) m->board_probe_rx_cycles, es);
                ExplainPropertyInteger("Board probe compute cycles", NULL,
                                       (int64) m->board_probe_compute_cycles, es);
                ExplainPropertyInteger("Board result emit cycles", NULL,
                                       (int64) m->board_result_emit_cycles, es);
                ExplainPropertyInteger("Board probe total cycles", NULL,
                                       (int64) m->board_probe_total_cycles, es);
                ExplainPropertyInteger("Board ACK emit cycles", NULL,
                                       (int64) m->board_ack_emit_cycles, es);
                ExplainPropertyInteger("Board RX wait cycles", NULL,
                                       (int64) m->board_rx_wait_cycles, es);
                ExplainPropertyInteger("Board TX blocked cycles", NULL,
                                       (int64) m->board_tx_blocked_cycles, es);
                ExplainPropertyInteger("Board protocol wait cycles", NULL,
                                       (int64) m->board_protocol_wait_cycles, es);
                ExplainPropertyInteger("Board max build batch cycles", NULL,
                                       (int64) m->board_max_build_batch_cycles, es);
                ExplainPropertyInteger("Board max probe batch cycles", NULL,
                                       (int64) m->board_max_probe_batch_cycles, es);
                ExplainPropertyInteger("Board max result frame cycles", NULL,
                                       (int64) m->board_max_result_frame_cycles, es);
                ExplainPropertyInteger("Board hash build inserts", NULL,
                                       (int64) m->board_hash_build_inserts, es);
                ExplainPropertyInteger("Board hash probe lookups", NULL,
                                       (int64) m->board_hash_probe_lookups, es);
                ExplainPropertyInteger("Board hash probe hits", NULL,
                                       (int64) m->board_hash_probe_hits, es);
                ExplainPropertyInteger("Board hash probe misses", NULL,
                                       (int64) m->board_hash_probe_misses, es);
                ExplainPropertyInteger("Board hash overflow errors", NULL,
                                       (int64) m->board_hash_overflow_errors, es);
                ExplainPropertyInteger("Board hash build collision steps", NULL,
                                       (int64) m->board_hash_build_collision_steps, es);
                ExplainPropertyInteger("Board hash probe collision steps", NULL,
                                       (int64) m->board_hash_probe_collision_steps, es);
                ExplainPropertyInteger("Board hash max build probe distance", NULL,
                                       (int64) m->board_hash_max_build_probe_distance, es);
                ExplainPropertyInteger("Board hash max probe distance", NULL,
                                       (int64) m->board_hash_max_probe_distance, es);
                ExplainPropertyInteger("Board hash table load factor ppm", NULL,
                                       (int64) m->board_hash_table_load_factor_ppm, es);
            }
        }
    }
    else
    {
        FPGA_LOG("fpga_hashjoin: before ExplainPropertyBool, fpga_simulation=%d", (int) fpga_simulation);
        ExplainPropertyBool("Simulation", fpga_simulation, es);
        if (!fpga_simulation)
        {
            ExplainPropertyText("Algorithm", fpga_algorithm, es);
            ExplainPropertyText("Transport", fpga_transport, es);
        }
        FPGA_LOG("fpga_hashjoin: after ExplainPropertyBool");
    }
    FPGA_LOG("fpga_hashjoin: explain_custom_scan done");
}
