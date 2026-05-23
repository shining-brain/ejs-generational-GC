#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="$ROOT/build.debug/benchmarks"
BASELINE_DIR="$BENCH_DIR/out_giy_rset_read_slot_fullsuite_20260519_001326"
OUT_BASE="${1:-$BENCH_DIR/out_giy_rset_read_slot_cache_miss_$(date +%Y%m%d_%H%M%S)}"
BENCH_CPU="${BENCH_CPU:-0}"
EVENTS="${EVENTS:-cycles,instructions,cache-references,cache-misses}"

if [[ "$OUT_BASE" != /* ]]; then
  OUT_BASE="$ROOT/$OUT_BASE"
fi

CONFIGS=(giy_default giy_rset_read_slot)
BENCHMARKS=(
  Bounce
  CD
  DeltaBlue
  Havlak
  List
  Mandelbrot
  NBody
  Permute
  Queens
  Richards
  Sieve
  Storage
  Towers
)

mkdir -p "$OUT_BASE"

{
  echo "root=$ROOT"
  echo "bench_dir=$BENCH_DIR"
  echo "baseline_dir=$BASELINE_DIR"
  echo "out_base=$OUT_BASE"
  echo "configs=${CONFIGS[*]}"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "bench_cpu=$BENCH_CPU"
  echo "events=$EVENTS"
  echo "started_at=$(date -Is)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed 's/^/lscpu: /'
  fi
  if [[ -r /proc/sys/kernel/perf_event_paranoid ]]; then
    echo "perf_event_paranoid=$(cat /proc/sys/kernel/perf_event_paranoid)"
  fi
  if [[ -r /proc/sys/kernel/nmi_watchdog ]]; then
    echo "nmi_watchdog=$(cat /proc/sys/kernel/nmi_watchdog)"
  fi
  if command -v perf >/dev/null 2>&1; then
    perf --version | sed 's/^/perf: /'
  fi
} > "$OUT_BASE/run.info"

run_one() {
  local config="$1"
  local bench="$2"
  local exe="$BASELINE_DIR/$config/ejsvm"
  local config_dir="$OUT_BASE/$config"
  local out_file="$config_dir/${bench}.out"
  local stderr_file="$config_dir/${bench}.stderr"
  local perf_file="$config_dir/${bench}.perf.csv"
  local status_file="$config_dir/${bench}.status"

  mkdir -p "$config_dir"

  if [[ ! -x "$exe" ]]; then
    echo "missing executable: $exe" >&2
    echo "missing_executable" > "$status_file"
    return 0
  fi
  if [[ ! -f "$BENCH_DIR/${bench}.sbc" ]]; then
    echo "missing benchmark: $BENCH_DIR/${bench}.sbc" >&2
    echo "missing_benchmark" > "$status_file"
    return 0
  fi

  echo "[$(date -Is)] run config=$config bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    perf stat -x, -o "$perf_file" -e "$EVENTS" -- \
      taskset -c "$BENCH_CPU" "$exe" "${bench}.sbc" \
      > "$out_file" 2> "$stderr_file"
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done config=$config bench=$bench status=$status"
  return 0
}

for config in "${CONFIGS[@]}"; do
  cp "$BASELINE_DIR/$config/ejsvm" "$OUT_BASE/$config.ejsvm" 2>/dev/null || true
  for bench in "${BENCHMARKS[@]}"; do
    run_one "$config" "$bench"
  done
done

echo "finished_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
