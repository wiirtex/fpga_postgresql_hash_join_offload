-- fpga_hashjoin UART smoke test.
--
-- Run from WSL after the board is programmed and the FT2232H UART channel is
-- attached with usbipd:
--
--   make -C pg uart_smoke CXX=/usr/bin/g++ FPGA_DEVICE=/dev/ttyUSB1

LOAD 'fpga_hashjoin';
SET fpga.enabled = on;
SET fpga.simulation = off;
SET fpga.device = :fpga_device;
SET fpga.device_baud = :fpga_baud;
SET fpga.min_inner_rows = 1;
SET fpga.warn_timeout_ms = 1000;
SET fpga.hard_timeout_ms = 10000;

DROP TABLE IF EXISTS fpga_test_emp, fpga_test_ord;
CREATE TABLE fpga_test_emp (emp_id int PRIMARY KEY, name text);
CREATE TABLE fpga_test_ord (order_id int, emp_id int, amount int);

INSERT INTO fpga_test_emp VALUES (1, 'Alice'), (2, 'Bob'), (3, 'Carol');
INSERT INTO fpga_test_ord VALUES
  (1, 1, 100), (2, 1, 200), (3, 2, 150), (4, 3, 300), (5, 3, 400);

EXPLAIN (COSTS OFF)
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id
  ORDER BY o.order_id;

CREATE TEMP TABLE _fpga AS
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id;

SET fpga.enabled = off;

CREATE TEMP TABLE _cpu AS
  SELECT e.emp_id, e.name, o.order_id, o.amount
  FROM fpga_test_emp e JOIN fpga_test_ord o ON e.emp_id = o.emp_id;

SELECT 'fpga_only' AS check, count(*) AS rows
  FROM (TABLE _fpga EXCEPT ALL TABLE _cpu) d;

SELECT 'cpu_only' AS check, count(*) AS rows
  FROM (TABLE _cpu EXCEPT ALL TABLE _fpga) d;

TABLE _fpga ORDER BY order_id;

DROP TABLE _fpga, _cpu;
DROP TABLE fpga_test_emp, fpga_test_ord;
