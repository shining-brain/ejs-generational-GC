#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"
OUT_BASE="${1:-$BENCH_DIR/out_giy_cache_size_$(date +%Y%m%d_%H%M%S)}"

SIZES=(512 640 768 896)
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
  echo "gc=giy"
  echo "sizes=${SIZES[*]}"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "started_at=$(date -Is)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed 's/^/lscpu: /'
  fi
} > "$OUT_BASE/run.info"

run_benchmark() {
  local size="$1"
  local bench="$2"
  local size_dir="$OUT_BASE/cache_${size}"
  local out_file="$size_dir/${bench}.out"
  local time_file="$size_dir/${bench}.time"
  local status_file="$size_dir/${bench}.status"

  if [[ -f "$status_file" ]]; then
    local old_status
    old_status="$(cat "$status_file" 2>/dev/null || true)"
    if [[ "$old_status" == "0" ]]; then
      echo "[$(date -Is)] skip CACHE_SIZE_KB=$size bench=$bench status=0"
      return 0
    fi
  fi

  echo "[$(date -Is)] run CACHE_SIZE_KB=$size bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    if command -v taskset >/dev/null 2>&1; then
      /usr/bin/time -p taskset -c "${BENCH_CPU:-0}" ../ejsvm "${bench}.sbc" > "$out_file" 2> "$time_file"
    else
      /usr/bin/time -p ../ejsvm "${bench}.sbc" > "$out_file" 2> "$time_file"
    fi
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done CACHE_SIZE_KB=$size bench=$bench status=$status"
  return 0
}

for size in "${SIZES[@]}"; do
  size_dir="$OUT_BASE/cache_${size}"
  mkdir -p "$size_dir"

  echo "[$(date -Is)] build GiY CACHE_SIZE_KB=$size"
  (
    cd "$BUILD_DIR" || exit 1
    make OPT_GC=giy CACHE_SIZE_KB="$size" -B -j2
  ) > "$size_dir/build.log" 2>&1
  build_status=$?
  echo "$build_status" > "$size_dir/build.status"
  if [[ "$build_status" != "0" ]]; then
    echo "[$(date -Is)] build failed CACHE_SIZE_KB=$size status=$build_status"
    exit "$build_status"
  fi
  cp "$BUILD_DIR/ejsvm" "$size_dir/ejsvm_giy_cache_${size}"

  for bench in "${BENCHMARKS[@]}"; do
    if [[ ! -f "$BENCH_DIR/${bench}.sbc" ]]; then
      echo "[$(date -Is)] missing benchmark $BENCH_DIR/${bench}.sbc"
      echo "missing" > "$size_dir/${bench}.status"
      continue
    fi
    run_benchmark "$size" "$bench"
  done
done

echo "finished_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
