#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"
OUT_BASE="${1:-$BENCH_DIR/out_giy_rset_read_slot_$(date +%Y%m%d_%H%M%S)}"
CACHE_SIZE_KB="${CACHE_SIZE_KB:-896}"
BENCH_CPU="${BENCH_CPU:-0}"

if [[ "$OUT_BASE" != /* ]]; then
  OUT_BASE="$ROOT/$OUT_BASE"
fi

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
  echo "build_dir=$BUILD_DIR"
  echo "bench_dir=$BENCH_DIR"
  echo "out_base=$OUT_BASE"
  echo "cache_size_kb=$CACHE_SIZE_KB"
  echo "bench_cpu=$BENCH_CPU"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "configs=giy_default giy_rset_read_slot"
  echo "giy_default_build_args=GIY_RSET_READ_SLOT_AT_GC=false"
  echo "giy_rset_read_slot_build_args=GIY_RSET_READ_SLOT_AT_GC=true"
  echo "started_at=$(date -Is)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed 's/^/lscpu: /'
  fi
} > "$OUT_BASE/run.info"

run_benchmark() {
  local config="$1"
  local bench="$2"
  local config_dir="$OUT_BASE/$config"
  local out_file="$config_dir/${bench}.out"
  local time_file="$config_dir/${bench}.time"
  local status_file="$config_dir/${bench}.status"

  if [[ ! -f "$BENCH_DIR/${bench}.sbc" ]]; then
    echo "missing" > "$status_file"
    echo "[$(date -Is)] missing config=$config bench=$bench"
    return 0
  fi

  echo "[$(date -Is)] run config=$config bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    if command -v taskset >/dev/null 2>&1; then
      /usr/bin/time -p taskset -c "$BENCH_CPU" ../ejsvm "${bench}.sbc" > "$out_file" 2> "$time_file"
    else
      /usr/bin/time -p ../ejsvm "${bench}.sbc" > "$out_file" 2> "$time_file"
    fi
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done config=$config bench=$bench status=$status"
  return 0
}

run_config() {
  local config="$1"
  shift
  local config_dir="$OUT_BASE/$config"
  mkdir -p "$config_dir"

  echo "[$(date -Is)] build config=$config CACHE_SIZE_KB=$CACHE_SIZE_KB args=$*"
  (
    cd "$BUILD_DIR" || exit 1
    make OPT_GC=giy CACHE_SIZE_KB="$CACHE_SIZE_KB" "$@" -B -j2
  ) > "$config_dir/build.log" 2>&1
  local build_status=$?
  echo "$build_status" > "$config_dir/build.status"
  if [[ "$build_status" != "0" ]]; then
    echo "[$(date -Is)] build failed config=$config status=$build_status"
    exit "$build_status"
  fi
  cp "$BUILD_DIR/ejsvm" "$config_dir/ejsvm"

  for bench in "${BENCHMARKS[@]}"; do
    run_benchmark "$config" "$bench"
  done
}

run_config giy_default GIY_RSET_READ_SLOT_AT_GC=false
run_config giy_rset_read_slot GIY_RSET_READ_SLOT_AT_GC=true

echo "finished_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
