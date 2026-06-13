-- fpga_hashjoin regression test
-- Validates that FPGA hash join produces identical results to CPU hash join.
--
-- Workflow:
--   1. make && sudo make install && make installcheck
--   2. First run will fail on Test 1 (EXPLAIN section not yet in .out).
--      Copy test/results/fpga_hashjoin.out EXPLAIN section into
--      test/expected/fpga_hashjoin.out, then re-run → PASS.
-- ── Setup ─────────────────────────────────────────────────────────────────────
LOAD 'fpga_hashjoin';
SET fpga.enabled        = on;
SET fpga.simulation     = on;
SET fpga.min_inner_rows = 1;

-- PRIMARY KEY ensures unique join key on the FPGA build side.
-- The FPGA hash table (linear probing) cannot handle duplicate inner keys.
CREATE TABLE fpga_test_emp (emp_id int PRIMARY KEY, name text);
CREATE TABLE fpga_test_ord (order_id int, emp_id int, amount int);

INSERT INTO fpga_test_emp VALUES (1,'Alice'),(2,'Bob'),(3,'Carol');
INSERT INTO fpga_test_ord VALUES
  (1,1,100),(2,1,200),(3,2,150),(4,3,300),(5,3,400);

-- ── Test 1: plan uses Custom Scan ─────────────────────────────────────────────
EXPLAIN (COSTS OFF)
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id
  ORDER BY o.order_id;

-- ── Test 2: FPGA vs CPU correctness (both diffs must be 0 rows) ───────────────
CREATE TEMP TABLE _fpga AS
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id;

SET fpga.enabled = off;

CREATE TEMP TABLE _cpu AS
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id;

SELECT 'fpga_only' AS check, count(*) AS rows
  FROM (TABLE _fpga EXCEPT ALL TABLE _cpu) d;

SELECT 'cpu_only'  AS check, count(*) AS rows
  FROM (TABLE _cpu  EXCEPT ALL TABLE _fpga) d;

-- ── Test 3a: ordered CPU result ───────────────────────────────────────────────
SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id
  ORDER BY o.order_id;

-- ── Test 3b: ordered FPGA result (must match 3a) ──────────────────────────────
SET fpga.enabled = on;

SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id
  ORDER BY o.order_id;

-- ── Cleanup ───────────────────────────────────────────────────────────────────
DROP TABLE _fpga, _cpu;
DROP TABLE fpga_test_emp, fpga_test_ord;
