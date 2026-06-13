/*
 * fpga_planner.c — planner hook and offloadability check.
 */

#include "postgres.h"

#include "nodes/extensible.h"
#include "nodes/pathnodes.h"
#include "nodes/pg_list.h"
#include "optimizer/pathnode.h"
#include "optimizer/paths.h"
#include "parser/parsetree.h"
#include "utils/lsyscache.h"

#include <string.h>

#include "fpga_adapter.h"    /* AdapterKeyType */
#include "fpga_internal.h"   /* fpga_planner_hook prototype */

#define FPGA_HT_MAX_ROWS             12288u
#define FPGA_GRACE_MAX_K             256u
#define FPGA_GRACE_WORDS_PER_TUPLE   2u
#define FPGA_GRACE_INNER_SLOT_WORDS  (FPGA_HT_MAX_ROWS * FPGA_GRACE_WORDS_PER_TUPLE)
#define FPGA_GRACE_OUTER_AREA_BASE   (FPGA_GRACE_MAX_K * FPGA_GRACE_INNER_SLOT_WORDS)
#define FPGA_GRACE_DDR2_TOTAL_WORDS  (16u * 1024u * 1024u)
#define FPGA_GRACE_MAX_INNER_ROWS    (FPGA_GRACE_MAX_K * FPGA_HT_MAX_ROWS)

/* GUC variables declared in fpga_hashjoin.c */
extern bool fpga_enabled;
extern char *fpga_algorithm;
extern int  fpga_min_inner_rows;
extern int  fpga_max_inner_rows;

/* CustomScanMethods defined in fpga_hashjoin.c */
extern const CustomScanMethods fpga_scan_methods;

static bool
fpga_algorithm_is_b(void)
{
    return fpga_algorithm != NULL &&
           (pg_strcasecmp(fpga_algorithm, "b") == 0 ||
            pg_strcasecmp(fpga_algorithm, "algorithm_b") == 0 ||
            pg_strcasecmp(fpga_algorithm, "grace") == 0);
}

static bool
fpga_algorithm_is_valid(void)
{
    if (fpga_algorithm == NULL)
        return true;

    return pg_strcasecmp(fpga_algorithm, "a") == 0 ||
           pg_strcasecmp(fpga_algorithm, "algorithm_a") == 0 ||
           pg_strcasecmp(fpga_algorithm, "linear") == 0 ||
           fpga_algorithm_is_b();
}

static double
fpga_grace_estimate_k(double inner_rows)
{
    uint64 rows;
    uint64 k;

    if (inner_rows <= 0.0)
        return 1.0;

    rows = (uint64) inner_rows;
    if ((double) rows < inner_rows)
        rows++;

    k = (rows + FPGA_HT_MAX_ROWS - 1u) / FPGA_HT_MAX_ROWS;
    if (k < 1u)
        k = 1u;
    return (double) k;
}

static bool
fpga_grace_estimate_fits(double inner_rows, double outer_rows)
{
    double k;
    uint64 outer_rows_u;
    double outer_slot_words;
    double used_words;

    if (inner_rows > (double) FPGA_GRACE_MAX_INNER_ROWS)
        return false;

    k = fpga_grace_estimate_k(inner_rows);
    if (k > (double) FPGA_GRACE_MAX_K)
        return false;

    outer_rows_u = (outer_rows <= 0.0) ? 0u : (uint64) outer_rows;
    if ((double) outer_rows_u < outer_rows)
        outer_rows_u++;

    outer_slot_words = ((outer_rows_u / (uint64) k) + 1u) *
                       (double) FPGA_GRACE_WORDS_PER_TUPLE * 2.0;
    used_words = (double) FPGA_GRACE_OUTER_AREA_BASE + k * outer_slot_words;

    return used_words <= (double) FPGA_GRACE_DDR2_TOTAL_WORDS;
}

/* ── Helper: find a single equijoin clause and extract key OID ────────────── */

static bool
find_equijoin_key(List               *restrictlist,
                  RelOptInfo         *innerrel,
                  RelOptInfo         *outerrel,
                  Oid                *out_key_oid,
                  AttrNumber         *out_inner_attno,
                  AttrNumber         *out_outer_attno)
{
    int         eq_count = 0;
    ListCell   *lc;

    *out_key_oid     = InvalidOid;
    *out_inner_attno = InvalidAttrNumber;
    *out_outer_attno = InvalidAttrNumber;

    foreach(lc, restrictlist)
    {
        RestrictInfo *rinfo = (RestrictInfo *) lfirst(lc);
        OpExpr       *opexpr;
        Var          *left_var, *right_var;
        Var          *inner_var, *outer_var;

        if (!IsA(rinfo, RestrictInfo))
            continue;

        /* Must be an operator expression */
        if (!IsA(rinfo->clause, OpExpr))
            continue;

        opexpr = (OpExpr *) rinfo->clause;

        /* Must have exactly 2 arguments, both Var nodes */
        if (list_length(opexpr->args) != 2)
            continue;

        left_var  = (Var *) linitial(opexpr->args);
        right_var = (Var *) lsecond(opexpr->args);

        if (!IsA(left_var, Var) || !IsA(right_var, Var))
            continue;

        /* hashjoinoperator is non-zero iff this is a hash-joinable equijoin */
        if (!OidIsValid(rinfo->hashjoinoperator))
            continue;

        /* Both sides must be from different relations */
        if (left_var->varno == right_var->varno)
            continue;

        eq_count++;
        if (eq_count > 1)
            return false;   /* more than one join clause — not supported */

        /* Determine which Var belongs to inner vs outer relation */
        if (bms_is_member(left_var->varno, innerrel->relids))
        {
            inner_var = left_var;
            outer_var = right_var;
        }
        else if (bms_is_member(left_var->varno, outerrel->relids))
        {
            inner_var = right_var;
            outer_var = left_var;
        }
        else
            continue;   /* neither side maps to inner/outer — skip */

        /* Both sides must have the same type */
        if (inner_var->vartype != outer_var->vartype)
            return false;

        *out_key_oid     = inner_var->vartype;
        *out_inner_attno = inner_var->varattno;
        *out_outer_attno = outer_var->varattno;
    }

    return (eq_count == 1);
}

/* ── rel_joinkey_is_unique ────────────────────────────────────────────────── */

/*
 * Returns true if 'rel' has a single-column UNIQUE or PRIMARY KEY index
 * (non-partial, IMMEDIATE) on attribute 'attno'.
 *
 * The FPGA hash table uses linear probing and raises ERR_DUPLICATE_KEY for
 * duplicate inner (build-side) keys.  We enforce this constraint at plan time
 * so we never offload a join where the build side might have duplicate keys.
 */
static bool
rel_joinkey_is_unique(RelOptInfo *rel, AttrNumber attno)
{
    ListCell *lc;
    foreach(lc, rel->indexlist)
    {
        IndexOptInfo *idx = (IndexOptInfo *) lfirst(lc);
        if (!idx->unique)                 continue;   /* not a unique index */
        if (!idx->immediate)              continue;   /* DEFERRED unique not safe */
        if (idx->ncolumns != 1)           continue;   /* must be single-column */
        if (idx->indexkeys[0] != attno)   continue;   /* must cover join key */
        if (idx->indpred != NIL)          continue;   /* partial index: not universal */
        return true;
    }
    return false;
}

/* ── fpga_is_offloadable ──────────────────────────────────────────────────── */

static bool
fpga_is_offloadable(PlannerInfo        *root,
                    RelOptInfo         *joinrel,
                    RelOptInfo         *outerrel,
                    RelOptInfo         *innerrel,
                    JoinType            jointype,
                    JoinPathExtraData  *extra,
                    AdapterKeyType     *out_key_type,
                    AttrNumber         *out_inner_attno,
                    AttrNumber         *out_outer_attno)
{
    Oid        key_oid;

    (void) root;
    (void) joinrel;

    /* Condition 1: master GUC switch */
    if (!fpga_enabled)
        return false;

    if (!fpga_algorithm_is_valid())
        return false;

    /* Condition 2: only INNER JOIN */
    if (jointype != JOIN_INNER)
        return false;

    /* Conditions 3+4: exactly one equijoin clause on INT4 or INT8 */
    if (!find_equijoin_key(extra->restrictlist,
                           innerrel, outerrel,
                           &key_oid, out_inner_attno, out_outer_attno))
        return false;

    if (key_oid == INT4OID)
        *out_key_type = ADAPTER_KEY_INT32;
    else if (key_oid == INT8OID)
        *out_key_type = ADAPTER_KEY_INT64;
    else
        return false;   /* unsupported key type */

    /* Condition 5: estimated input sizes fit the selected hardware layout */
    if (fpga_algorithm_is_b())
    {
        if (!fpga_grace_estimate_fits(innerrel->rows, outerrel->rows))
            return false;
    }
    else if (innerrel->rows > (double) fpga_max_inner_rows)
        return false;

    /* Condition 6: inner row estimate meets minimum threshold */
    if (innerrel->rows < (double) fpga_min_inner_rows)
        return false;

    /* Condition 7: both must be simple base relations */
    if (innerrel->reloptkind != RELOPT_BASEREL ||
        outerrel->reloptkind != RELOPT_BASEREL)
        return false;

    return true;
}

/* ── PlanCustomPath ───────────────────────────────────────────────────────── */

/*
 * Convert the CustomPath into a CustomScan plan node.
 * PostgreSQL calls this when it selects our path as the winner.
 *
 * We use the custom_scan_tlist / INDEX_VAR pattern:
 *   - custom_scan_tlist holds the original Vars (real varno = RTI).
 *     ExecInitCustomScan creates ss_ScanTupleSlot from this list.
 *   - scan.plan.targetlist holds INDEX_VAR(col) refs into that slot.
 *     ExecAssignScanProjectionInfoWithVarno builds ProjectionInfo that
 *     reads col-1 from ss_ScanTupleSlot.
 * In exec_custom_scan we fill ss_ScanTupleSlot manually from inner/outer
 * fetch slots, then call ExecProject — no outerPlan/innerPlan needed.
 */
static Plan *
plan_fpga_custom_path(PlannerInfo *root,
                      RelOptInfo  *rel,
                      CustomPath  *best_path,
                      List        *tlist,
                      List        *clauses,
                      List        *custom_plans)
{
    CustomScan *cscan = makeNode(CustomScan);

    (void) root;
    (void) rel;
    (void) clauses;

    /*
     * custom_scan_tlist: the "physical" output tuple — original Vars with
     * real relation varnos.  ExecInitCustomScan builds ss_ScanTupleSlot
     * from this descriptor.
     *
     * scan.plan.targetlist: set to the same original Vars here.
     * set_plan_references will call fix_upper_expr on it, find each Var in
     * the indexed custom_scan_tlist, and rewrite them as INDEX_VAR(col)
     * references.  By execution time the targetlist has INDEX_VAR Vars and
     * ExecAssignScanProjectionInfoWithVarno builds the right ProjectionInfo.
     *
     * Do NOT convert to INDEX_VAR manually here — setrefs must do it so that
     * varno rtoffset arithmetic is applied consistently.
     */
    FPGA_LOG("fpga_hashjoin: plan_fpga_custom_path called, tlist len=%d, custom_plans len=%d",
         list_length(tlist), list_length(custom_plans));

    cscan->custom_scan_tlist = tlist;

    cscan->scan.scanrelid       = 0;        /* not a simple base-rel scan */
    cscan->scan.plan.targetlist = tlist;
    cscan->scan.plan.qual       = NIL;
    cscan->custom_plans         = custom_plans;
    cscan->custom_private       = best_path->custom_private;
    cscan->methods              = &fpga_scan_methods;

    FPGA_LOG("fpga_hashjoin: plan_fpga_custom_path done, cscan=%p methods=%p",
         (void *) cscan, (void *) cscan->methods);

    return (Plan *) cscan;
}

static const CustomPathMethods fpga_path_methods = {
    "fpga_hashjoin",        /* CustomName */
    plan_fpga_custom_path,  /* PlanCustomPath */
    NULL                    /* TextOutCustomPath */
};

/* ── fpga_planner_hook ────────────────────────────────────────────────────── */

/* Prototype declared in fpga_internal.h to avoid -Wmissing-prototypes */
void
fpga_planner_hook(PlannerInfo        *root,
                  RelOptInfo         *joinrel,
                  RelOptInfo         *outerrel,
                  RelOptInfo         *innerrel,
                  JoinType            jointype,
                  JoinPathExtraData  *extra)
{
    AdapterKeyType  key_type;
    AttrNumber      inner_attno, outer_attno;
    CustomPath     *cpath;
    List           *custom_private;

    if (!fpga_is_offloadable(root, joinrel, outerrel, innerrel,
                              jointype, extra,
                              &key_type, &inner_attno, &outer_attno))
        return;

    /*
     * FPGA hash table (linear probing) raises ERR_DUPLICATE_KEY for duplicate
     * inner (build-side) keys.  Require the inner side to have a UNIQUE or
     * PRIMARY KEY constraint on the join column.
     *
     * If the optimizer gave us the "wrong" assignment (non-unique inner,
     * unique outer), swap inner ↔ outer so the unique side becomes the
     * hash-table build side.  Both paths are tried by the optimizer anyway;
     * we pick the assignment where the unique side is always inner.
     */
    if (!rel_joinkey_is_unique(innerrel, inner_attno))
    {
        /* Try the reversed assignment */
        if (!rel_joinkey_is_unique(outerrel, outer_attno))
            return;     /* neither side has unique join key — can't offload */
        if (fpga_algorithm_is_b())
        {
            if (!fpga_grace_estimate_fits(outerrel->rows, innerrel->rows))
                return; /* swapped layout would exceed DDR2 capacity */
        }
        else if (outerrel->rows > (double) fpga_max_inner_rows)
            return;     /* swapped inner would exceed BRAM capacity */

        /* Swap: outerrel becomes the new inner (unique key side) */
        {
            RelOptInfo *tr;
            AttrNumber  ta;

            tr = innerrel;
            ta = inner_attno;
            innerrel = outerrel;
            outerrel = tr;
            inner_attno = outer_attno;
            outer_attno = ta;
        }
    }

    /*
     * Encode key_type and attribute numbers into custom_private so the
     * executor can read them from the CustomScan plan node.
     */
    custom_private = list_make3_int((int) key_type,
                                    (int) inner_attno,
                                    (int) outer_attno);

    cpath = makeNode(CustomPath);
    cpath->path.pathtype       = T_CustomScan;
    cpath->path.parent         = joinrel;
    cpath->path.pathtarget     = joinrel->reltarget;
    cpath->path.param_info     = NULL;
    cpath->path.parallel_aware = false;
    cpath->path.parallel_safe  = false;
    cpath->path.rows           = joinrel->rows;

    /*
     * Cost model (MVP): set cost to 0 so the FPGA path is always chosen when
     * enabled.  A proper cost model based on UART throughput and FPGA latency
     * is a post-MVP improvement.
     */
    cpath->path.startup_cost   = 0.0;
    cpath->path.total_cost     = 0.0;

    cpath->flags               = 0;
    cpath->custom_paths        = list_make2(outerrel->cheapest_total_path,
                                             innerrel->cheapest_total_path);
    cpath->custom_private      = custom_private;
    cpath->methods             = &fpga_path_methods;

    add_path(joinrel, (Path *) cpath);
}
