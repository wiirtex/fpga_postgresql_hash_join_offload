#!/usr/bin/env python3
import argparse
import csv
import statistics
import re
from pathlib import Path


CASE_RE = re.compile(
    r"^########\s+(\S+)\s+inner=(\d+)\s+outer=(\d+)"
    r"(?:\s+pattern=(\S+))?"
    r"(?:\s+expected=(\d+))?"
    r"(?:\s+run=(\d+)\s+warmup=([01]))?\s+########"
)
EXEC_RE = re.compile(r"Execution Time:\s+([0-9.]+)\s+ms")
FPGA_ROWS_RE = re.compile(r"^\s*(\d+)\s+\|\s+(\d+)\s+\|\s+([tf])\s*$")
CPU_ROWS_RE = re.compile(r"^\s*(\d+)\s*$")
META_RE = re.compile(r"^([A-Za-z0-9_.-]+)=(.*)$")
EXPLAIN_FIELD_RE = re.compile(r"^\s*([^:]+):\s+([0-9.]+)(?:\s+ms)?\s*$")

EXPLAIN_FIELDS = {
    "FPGA time (ms)": "fpga_time_ms",
    "Inner drain time (ms)": "inner_drain_ms",
    "Outer drain time (ms)": "outer_drain_ms",
    "Adapter run time (ms)": "adapter_run_ms",
    "Result copy time (ms)": "result_copy_ms",
    "TID fetch time (ms)": "tid_fetch_ms",
    "Custom scan measured time (ms)": "custom_scan_ms",
    "Host config send time (ms)": "host_config_send_ms",
    "Host config ACK wait time (ms)": "host_config_ack_wait_ms",
    "Host build send time (ms)": "host_build_send_ms",
    "Host build ACK wait time (ms)": "host_build_ack_wait_ms",
    "Host probe send time (ms)": "host_probe_send_ms",
    "Host probe ACK wait time (ms)": "host_probe_ack_wait_ms",
    "Host final STATUS wait time (ms)": "host_final_status_wait_ms",
    "Host reset wait time (ms)": "host_reset_wait_ms",
    "Host result receive time (ms)": "host_result_recv_ms",
    "Host protocol frames sent": "host_protocol_frames_sent",
    "Host protocol frames received": "host_protocol_frames_recv",
    "Host transport sends": "host_transport_sends",
    "Host bytes sent": "host_bytes_sent",
    "Host bytes received": "host_bytes_recv",
    "Host inner frames sent": "host_inner_frames_sent",
    "Host outer frames sent": "host_outer_frames_sent",
    "Host ACK frames received": "host_ack_frames_recv",
    "Host STATUS frames received": "host_status_frames_recv",
    "Host RESULT frames received": "host_result_frames_recv",
    "Host DEBUG frames received": "host_debug_frames_recv",
    "Host TIMING frames received": "host_timing_frames_recv",
    "Host result pairs received": "host_result_pairs_recv",
    "Board timing version": "board_timing_version",
    "Board timing flags": "board_timing_flags",
    "Board clock Hz": "board_clock_hz",
    "Board inner rows": "board_inner_rows",
    "Board outer rows": "board_outer_rows",
    "Board matched rows": "board_matched_rows",
    "Board inner frames": "board_inner_frames",
    "Board outer frames": "board_outer_frames",
    "Board result frames": "board_result_frames",
    "Board ACK frames": "board_ack_frames",
    "Board DEBUG frames": "board_debug_frames",
    "Board bytes RX": "board_bytes_rx",
    "Board bytes TX": "board_bytes_tx",
    "Board session total cycles": "board_session_total_cycles",
    "Board config cycles": "board_config_cycles",
    "Board build RX cycles": "board_build_rx_cycles",
    "Board build compute cycles": "board_build_compute_cycles",
    "Board build total cycles": "board_build_total_cycles",
    "Board probe RX cycles": "board_probe_rx_cycles",
    "Board probe compute cycles": "board_probe_compute_cycles",
    "Board result emit cycles": "board_result_emit_cycles",
    "Board probe total cycles": "board_probe_total_cycles",
    "Board ACK emit cycles": "board_ack_emit_cycles",
    "Board RX wait cycles": "board_rx_wait_cycles",
    "Board TX blocked cycles": "board_tx_blocked_cycles",
    "Board protocol wait cycles": "board_protocol_wait_cycles",
    "Board max build batch cycles": "board_max_build_batch_cycles",
    "Board max probe batch cycles": "board_max_probe_batch_cycles",
    "Board max result frame cycles": "board_max_result_frame_cycles",
    "Board hash build inserts": "board_hash_build_inserts",
    "Board hash probe lookups": "board_hash_probe_lookups",
    "Board hash probe hits": "board_hash_probe_hits",
    "Board hash probe misses": "board_hash_probe_misses",
    "Board hash overflow errors": "board_hash_overflow_errors",
    "Board hash build collision steps": "board_hash_build_collision_steps",
    "Board hash probe collision steps": "board_hash_probe_collision_steps",
    "Board hash max build probe distance": "board_hash_max_build_probe_distance",
    "Board hash max probe distance": "board_hash_max_probe_distance",
    "Board hash table load factor ppm": "board_hash_table_load_factor_ppm",
}

METRIC_COLUMNS = [
    "fpga_time_ms",
    "inner_drain_ms",
    "outer_drain_ms",
    "adapter_run_ms",
    "result_copy_ms",
    "tid_fetch_ms",
    "custom_scan_ms",
    "host_config_send_ms",
    "host_config_ack_wait_ms",
    "host_build_send_ms",
    "host_build_ack_wait_ms",
    "host_probe_send_ms",
    "host_probe_ack_wait_ms",
    "host_final_status_wait_ms",
    "host_reset_wait_ms",
    "host_result_recv_ms",
    "host_protocol_frames_sent",
    "host_protocol_frames_recv",
    "host_transport_sends",
    "host_bytes_sent",
    "host_bytes_recv",
    "host_inner_frames_sent",
    "host_outer_frames_sent",
    "host_ack_frames_recv",
    "host_status_frames_recv",
    "host_result_frames_recv",
    "host_debug_frames_recv",
    "host_timing_frames_recv",
    "host_result_pairs_recv",
    "board_timing_version",
    "board_timing_flags",
    "board_clock_hz",
    "board_inner_rows",
    "board_outer_rows",
    "board_matched_rows",
    "board_inner_frames",
    "board_outer_frames",
    "board_result_frames",
    "board_ack_frames",
    "board_debug_frames",
    "board_bytes_rx",
    "board_bytes_tx",
    "board_session_total_cycles",
    "board_config_cycles",
    "board_build_rx_cycles",
    "board_build_compute_cycles",
    "board_build_total_cycles",
    "board_probe_rx_cycles",
    "board_probe_compute_cycles",
    "board_result_emit_cycles",
    "board_probe_total_cycles",
    "board_ack_emit_cycles",
    "board_rx_wait_cycles",
    "board_tx_blocked_cycles",
    "board_protocol_wait_cycles",
    "board_max_build_batch_cycles",
    "board_max_probe_batch_cycles",
    "board_max_result_frame_cycles",
    "board_hash_build_inserts",
    "board_hash_probe_lookups",
    "board_hash_probe_hits",
    "board_hash_probe_misses",
    "board_hash_overflow_errors",
    "board_hash_build_collision_steps",
    "board_hash_probe_collision_steps",
    "board_hash_max_build_probe_distance",
    "board_hash_max_probe_distance",
    "board_hash_table_load_factor_ppm",
]


def infer_mode(path: Path) -> str:
    name = path.name.lower()
    if name.startswith("cpu_") or name.startswith("pg_cpu_") or "_cpu_" in name:
        return "cpu"
    if name.startswith("udp_") or name.startswith("pg_udp_") or "_udp_" in name:
        return "udp"
    if name.startswith("uart_") or name.startswith("pg_uart_") or "_uart_" in name:
        return "uart"
    return "unknown"


def parse_file(path: Path):
    rows = []
    current = None
    lines = path.read_text(errors="replace").splitlines()
    mode = infer_mode(path)
    metadata = {}

    for idx, line in enumerate(lines):
        if current is None:
            m_meta = META_RE.match(line)
            if m_meta:
                metadata[m_meta.group(1)] = m_meta.group(2)

        m = CASE_RE.match(line)
        if m:
            current = {
                "mode": mode,
                "label": m.group(1),
                "inner": int(m.group(2)),
                "outer": int(m.group(3)),
                "pattern": m.group(4) or "full",
                "expected": int(m.group(5)) if m.group(5) else None,
                "run": int(m.group(6) or 1),
                "warmup": (m.group(7) == "1"),
                "execution_ms": None,
                "capture_execution": mode != "uart",
                "rows": None,
                "rows_match": None,
                "source": str(path),
                "metadata": metadata.copy(),
            }
            for field in METRIC_COLUMNS:
                current[field] = None
            rows.append(current)
            continue

        if current is None:
            continue

        if mode == "uart" and "---- CPU baseline ----" in line:
            current["capture_execution"] = False
            continue

        if mode == "uart" and "---- FPGA UART ----" in line:
            current["capture_execution"] = True
            continue

        m = EXEC_RE.search(line)
        if m and current["capture_execution"] and current["execution_ms"] is None:
            current["execution_ms"] = float(m.group(1))
            continue

        m = FPGA_ROWS_RE.match(line)
        if m:
            current["rows"] = int(m.group(1))
            current["rows_match"] = (m.group(3) == "t")
            continue

        m = EXPLAIN_FIELD_RE.match(line)
        if m:
            key = EXPLAIN_FIELDS.get(m.group(1).strip())
            if key:
                value = float(m.group(2))
                if not key.endswith("_ms") and value.is_integer():
                    value = int(value)
                current[key] = value
                continue

        if "---- correctness count ----" in line and mode == "cpu":
            for lookahead in lines[idx + 1: idx + 12]:
                m = CPU_ROWS_RE.match(lookahead)
                if m:
                    current["rows"] = int(m.group(1))
                    current["rows_match"] = True
                    break

    return rows


def fmt_ms(value):
    if value is None:
        return "n/a"
    return f"{value:.3f}"


def fmt_ratio(value):
    if value is None:
        return "n/a"
    if 0 < value < 0.01:
        return f"{value:.4f}x"
    return f"{value:.2f}x"


def stats(values):
    values = [v for v in values if v is not None]
    if not values:
        return None
    return {
        "n": len(values),
        "min": min(values),
        "median": statistics.median(values),
        "avg": statistics.fmean(values),
        "stddev": statistics.stdev(values) if len(values) > 1 else 0.0,
    }


def summarize(rows):
    by_case = {}
    measured = [row for row in rows if not row.get("warmup")]
    for row in measured:
        key = (row["label"], row["inner"], row["outer"], row["pattern"], row["expected"])
        by_case.setdefault(key, {}).setdefault(row["mode"], []).append(row)
    return by_case


def mode_stats(modes, mode):
    return stats([row.get("execution_ms") for row in modes.get(mode, [])])


def mode_metric_stats(modes, mode, field):
    return stats([row.get(field) for row in modes.get(mode, [])])


def mode_rows(modes, mode):
    rows = [row.get("rows") for row in modes.get(mode, []) if row.get("rows") is not None]
    if not rows:
        return None
    if len(set(rows)) == 1:
        return rows[0]
    return "mixed"


def mode_correct(modes, mode):
    values = [row.get("rows_match") for row in modes.get(mode, [])
              if row.get("rows_match") is not None]
    if not values:
        return None
    return all(values)


def mode_expected(modes):
    values = [row.get("expected") for rows in modes.values()
              for row in rows if row.get("expected") is not None]
    if not values:
        return None
    if len(set(values)) == 1:
        return values[0]
    return "mixed"


def append_markdown(out, by_case, files):
    out.append("# Benchmark Summary")
    out.append("")
    out.append("| Case | Pattern | Inner | Outer | Expected rows | CPU median ms | UART median ms | UDP median ms | CPU/UDP speedup | UDP/CPU | Runs | Rows | Correct |")
    out.append("| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |")

    for (label, inner, outer, pattern, expected), modes in sorted(by_case.items(),
                                              key=lambda item: (item[0][1], item[0][2], item[0][0])):
        cpu = mode_stats(modes, "cpu")
        uart = mode_stats(modes, "uart")
        udp = mode_stats(modes, "udp")

        cpu_ms = cpu["median"] if cpu else None
        udp_ms = udp["median"] if udp else None
        cpu_udp_speedup = None
        udp_cpu_ratio = None
        if cpu_ms and udp_ms:
            cpu_udp_speedup = cpu_ms / udp_ms
            udp_cpu_ratio = udp_ms / cpu_ms

        run_counts = [s["n"] for s in (cpu, uart, udp) if s]
        runs = min(run_counts) if run_counts else 0
        rows_count = mode_rows(modes, "udp")
        if rows_count is None:
            rows_count = mode_rows(modes, "uart")
        if rows_count is None:
            rows_count = mode_rows(modes, "cpu")
        if expected is None:
            expected = mode_expected(modes)

        correctness = []
        for mode in ("cpu", "uart", "udp"):
            correct = mode_correct(modes, mode)
            if correct is not None:
                correctness.append(correct)
        correct_all = all(correctness) if correctness else None

        out.append(
            f"| {label} | {pattern} | {inner} | {outer} | "
            f"{expected if expected is not None else 'n/a'} | "
            f"{fmt_ms(cpu_ms)} | "
            f"{fmt_ms(uart['median'] if uart else None)} | "
            f"{fmt_ms(udp_ms)} | {fmt_ratio(cpu_udp_speedup)} | "
            f"{fmt_ratio(udp_cpu_ratio)} | {runs} | "
            f"{rows_count if rows_count is not None else 'n/a'} | "
            f"{'yes' if correct_all else 'no' if correct_all is not None else 'n/a'} |"
        )

    out.append("")
    out.append("## Detailed Timing Statistics")
    out.append("")
    out.append("| Case | Mode | Runs | Min ms | Median ms | Avg ms | Stddev ms |")
    out.append("| --- | --- | ---: | ---: | ---: | ---: | ---: |")
    for (label, inner, outer, pattern, expected), modes in sorted(by_case.items(),
                                              key=lambda item: (item[0][1], item[0][2], item[0][0])):
        del inner, outer, expected
        for mode in ("cpu", "uart", "udp"):
            s = mode_stats(modes, mode)
            if not s:
                continue
            out.append(
                f"| {label} ({pattern}) | {mode} | {s['n']} | {fmt_ms(s['min'])} | "
                f"{fmt_ms(s['median'])} | {fmt_ms(s['avg'])} | "
                f"{fmt_ms(s['stddev'])} |"
            )

    out.append("")
    out.append("## Source Files")
    out.append("")
    for path in files:
        out.append(f"- `{path}`")


def write_csv(path: Path, by_case):
    with path.open("w", newline="") as f:
        writer = csv.writer(f)
        headers = [
            "case", "inner", "outer", "mode", "runs", "min_ms",
            "median_ms", "avg_ms", "stddev_ms", "pattern",
            "expected_rows", "rows", "correct",
        ]
        headers.extend(f"{field}_median" for field in METRIC_COLUMNS)
        writer.writerow(headers)
        for (label, inner, outer, pattern, expected), modes in sorted(by_case.items(),
                                                  key=lambda item: (item[0][1], item[0][2], item[0][0])):
            for mode in ("cpu", "uart", "udp"):
                s = mode_stats(modes, mode)
                if not s:
                    continue
                values = [
                    label, inner, outer, mode, s["n"], f"{s['min']:.6f}",
                    f"{s['median']:.6f}", f"{s['avg']:.6f}",
                    f"{s['stddev']:.6f}", pattern, expected,
                    mode_rows(modes, mode),
                    mode_correct(modes, mode),
                ]
                for field in METRIC_COLUMNS:
                    metric = mode_metric_stats(modes, mode, field)
                    if metric:
                        values.append(f"{metric['median']:.6f}")
                    else:
                        values.append("")
                writer.writerow(values)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("-o", "--output", type=Path)
    parser.add_argument("--csv-output", type=Path)
    args = parser.parse_args()

    rows = []
    for path in args.files:
        rows.extend(parse_file(path))

    by_case = summarize(rows)

    out = []
    append_markdown(out, by_case, args.files)

    text = "\n".join(out) + "\n"
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end="")
    if args.csv_output:
        write_csv(args.csv_output, by_case)


if __name__ == "__main__":
    main()
