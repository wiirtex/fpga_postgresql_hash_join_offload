#!/usr/bin/env bash
set -euo pipefail

DB="${PGDATABASE:-postgres}"
PSQL="${PSQL:-psql}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PG_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_DIR="$(cd "$PG_DIR/.." && pwd)"
RESULT_DIR="$SCRIPT_DIR/results"
mkdir -p "$RESULT_DIR"

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT="$RESULT_DIR/cpu_bench_${STAMP}.txt"
BENCH_RUNS="${BENCH_RUNS:-5}"
BENCH_WARMUP="${BENCH_WARMUP:-1}"
GIT_COMMIT="$(git -C "$PG_DIR/.." rev-parse --short HEAD 2>/dev/null || echo unknown)"
GIT_BRANCH="$(git -C "$PG_DIR/.." rev-parse --abbrev-ref HEAD 2>/dev/null || echo unknown)"
BITSTREAM_PATH="${BITSTREAM_PATH:-}"
BITSTREAM_TIMESTAMP="n/a"
if [[ -n "$BITSTREAM_PATH" && -e "$BITSTREAM_PATH" ]]; then
  BITSTREAM_TIMESTAMP="$(stat -c '%y' "$BITSTREAM_PATH" 2>/dev/null || echo n/a)"
fi

if [[ -n "${BENCH_CASES_FILE:-}" ]]; then
  cases_file="$BENCH_CASES_FILE"
  if [[ ! -f "$cases_file" && -f "$REPO_DIR/$cases_file" ]]; then
    cases_file="$REPO_DIR/$cases_file"
  fi
  mapfile -t cases <"$cases_file"
elif [[ -n "${BENCH_CASES:-}" ]]; then
  mapfile -t cases <<<"$BENCH_CASES"
else
  mapfile -t cases <"$SCRIPT_DIR/cases/default.txt"
fi

{
  echo "CPU benchmark started at $(date --iso-8601=seconds)"
  echo "db=$DB"
  echo "mode=cpu"
  echo "algorithm=postgresql"
  echo "git_branch=$GIT_BRANCH"
  echo "git_commit=$GIT_COMMIT"
  echo "bench_runs=$BENCH_RUNS"
  echo "bench_warmup=$BENCH_WARMUP"
  echo "bench_cases_file=${BENCH_CASES_FILE:-n/a}"
  echo "bitstream_path=${BITSTREAM_PATH:-n/a}"
  echo "bitstream_timestamp=$BITSTREAM_TIMESTAMP"
  echo
} | tee "$OUT"

for case in "${cases[@]}"; do
  read -r label inner_n outer_n pattern <<<"$case"
  pattern="${pattern:-full}"
  case "$pattern" in
    full|hotkey|outer_skew) expected_rows="$outer_n" ;;
    half) expected_rows=$((outer_n / 2)) ;;
    low10) expected_rows=$((outer_n / 10)) ;;
    none) expected_rows=0 ;;
    *) echo "Unsupported benchmark pattern: $pattern" >&2; exit 1 ;;
  esac
  total_runs=$((BENCH_WARMUP + BENCH_RUNS))
  for ((run_id = 1; run_id <= total_runs; run_id++)); do
    warmup=0
    measured_run="$run_id"
    if (( run_id <= BENCH_WARMUP )); then
      warmup=1
      measured_run=0
    else
      measured_run=$((run_id - BENCH_WARMUP))
    fi

    {
      echo
      echo "######## $label inner=$inner_n outer=$outer_n pattern=$pattern expected=$expected_rows run=$measured_run warmup=$warmup ########"
    } | tee -a "$OUT"

    PGOPTIONS="-cdynamic_library_path=$PG_DIR" \
    "$PSQL" -d "$DB" -v ON_ERROR_STOP=1 \
      -v bench_mode="'cpu'" \
      -v bench_algorithm="'a'" \
      -v fpga_ack_window="1" \
      -v fpga_max_batch="118" \
      -v fpga_device="'unused'" \
      -v fpga_baud="115200" \
      -v label="$label" \
      -v inner_n="$inner_n" \
      -v outer_n="$outer_n" \
      -v bench_pattern="'$pattern'" \
      -v cpu_only=1 \
      -f "$SCRIPT_DIR/hashjoin_bench.sql" 2>&1 | tee -a "$OUT"
  done
done

echo
echo "Wrote $OUT"
