-- UART benchmark case for fpga_hashjoin.
--
-- psql variables:
--   fpga_device  e.g. '/dev/ttyUSB1'
--   fpga_baud    e.g. 115200
--   inner_n      unique build-side rows, must be <= HT_MAX_ROWS for Algorithm A
--   outer_n      probe-side rows
--   bench_pattern full | half | low10 | none | hotkey | outer_skew
--   label        free-form label printed in the report

LOAD 'fpga_hashjoin';

\timing on
\echo ==== CASE :label inner=:inner_n outer=:outer_n pattern=:bench_pattern device=:fpga_device baud=:fpga_baud ====

SET client_min_messages = warning;
SET fpga.device = :fpga_device;
SET fpga.device_baud = :fpga_baud;
SET fpga.min_inner_rows = 1;
SET fpga.max_inner_rows = 12288;
SET fpga.warn_timeout_ms = 5000;
SET fpga.hard_timeout_ms = 120000;
SET fpga.explain_verbose = on;

DROP TABLE IF EXISTS fpga_bench_inner;
DROP TABLE IF EXISTS fpga_bench_outer;

CREATE TEMP TABLE fpga_bench_inner (
    k int PRIMARY KEY,
    payload int
);

CREATE TEMP TABLE fpga_bench_outer (
    id int,
    k int,
    payload int
);

INSERT INTO fpga_bench_inner
SELECT g, g * 10
  FROM generate_series(1, :inner_n) AS g;

INSERT INTO fpga_bench_outer
SELECT g,
       CASE :bench_pattern
         WHEN 'full' THEN ((g - 1) % :inner_n) + 1
         WHEN 'half' THEN
           CASE WHEN (g % 2) = 0 THEN ((g - 1) % :inner_n) + 1
                ELSE :inner_n + g
           END
         WHEN 'low10' THEN
           CASE WHEN (g % 10) = 0 THEN ((g - 1) % :inner_n) + 1
                ELSE :inner_n + g
           END
         WHEN 'none' THEN :inner_n + g
         WHEN 'hotkey' THEN 1
         WHEN 'outer_skew' THEN
           CASE WHEN g <= (:outer_n * 9) / 10 THEN 1
                ELSE ((g - 1) % :inner_n) + 1
           END
         ELSE (1 / 0)
       END,
       g * 100
  FROM generate_series(1, :outer_n) AS g;

ANALYZE fpga_bench_inner;
ANALYZE fpga_bench_outer;

\echo ---- CPU baseline ----
SET fpga.enabled = off;
SET enable_hashjoin = on;
SET enable_mergejoin = off;
SET enable_nestloop = off;

EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, SUMMARY ON, TIMING ON)
SELECT count(*)
  FROM fpga_bench_inner i
  JOIN fpga_bench_outer o ON i.k = o.k;

\echo ---- FPGA UART ----
SET fpga.enabled = on;
SET fpga.simulation = off;
SET enable_hashjoin = on;
SET enable_mergejoin = off;
SET enable_nestloop = off;

EXPLAIN (ANALYZE, BUFFERS, COSTS OFF, SUMMARY ON, TIMING ON)
SELECT count(*)
  FROM fpga_bench_inner i
  JOIN fpga_bench_outer o ON i.k = o.k;

\echo ---- correctness ----
SET fpga.enabled = on;
CREATE TEMP TABLE _fpga_count AS
SELECT count(*) AS c
  FROM fpga_bench_inner i
  JOIN fpga_bench_outer o ON i.k = o.k;

SET fpga.enabled = off;
CREATE TEMP TABLE _cpu_count AS
SELECT count(*) AS c
  FROM fpga_bench_inner i
  JOIN fpga_bench_outer o ON i.k = o.k;

SELECT _fpga_count.c AS fpga_rows,
       _cpu_count.c AS cpu_rows,
       (_fpga_count.c = _cpu_count.c) AS rows_match
  FROM _fpga_count, _cpu_count;

DROP TABLE _fpga_count;
DROP TABLE _cpu_count;
DROP TABLE fpga_bench_inner;
DROP TABLE fpga_bench_outer;
