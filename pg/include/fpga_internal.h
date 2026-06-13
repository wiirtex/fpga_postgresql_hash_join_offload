#ifndef FPGA_INTERNAL_H
#define FPGA_INTERNAL_H

/*
 * fpga_internal.h — shared prototypes for functions defined across multiple
 * translation units within the fpga_hashjoin extension.
 *
 * Include in every .c file that defines or calls these functions to satisfy
 * -Wmissing-prototypes.
 */

#include "postgres.h"
#include "nodes/extensible.h"
#include "optimizer/paths.h"

/*
 * Debug logging.
 * Build with -DFPGA_DEBUG (see Makefile) to enable.
 * Disabled by default to keep production logs clean.
 */
#ifdef FPGA_DEBUG
#define FPGA_LOG(...) elog(LOG, __VA_ARGS__)
#else
#define FPGA_LOG(...) ((void) 0)
#endif

/* Defined in fpga_planner.c, called from fpga_hashjoin.c hook wrapper */
void fpga_planner_hook(PlannerInfo *root,
                       RelOptInfo  *joinrel,
                       RelOptInfo  *outerrel,
                       RelOptInfo  *innerrel,
                       JoinType     jointype,
                       JoinPathExtraData *extra);

/* Defined in fpga_executor.c, used by CustomScanMethods in fpga_hashjoin.c */
Node *fpga_create_custom_scan_state(CustomScan *cscan);

#endif /* FPGA_INTERNAL_H */
