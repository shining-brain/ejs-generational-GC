#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"
OUT_BASE="${1:-$BENCH_DIR/out_giysb_tiny16_adaptive_896_$(date +%Y%m%d_%H%M%S)}"
case "$OUT_BASE" in
  /*) ;;
  *) OUT_BASE="$ROOT/$OUT_BASE" ;;
esac

CACHE_SIZE_KB_VALUE=896
GIY_GC_STACK_BYTES_VALUE=49152
GIYSB_STAGING_BYTES_VALUE=8192
GIYSB_TINY_TABLE_BYTES_VALUE=16384
GIYSB_TINY_OBJECT_MAX_BYTES_VALUE=64
GIYSB_TINY_STAGING_MIN_BYTES_VALUE=4096

CONFIGS=(
  giysb_tiny16_adaptive_896
)

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
  echo "cache_size_kb=$CACHE_SIZE_KB_VALUE"
  echo "giy_gc_stack_bytes=$GIY_GC_STACK_BYTES_VALUE"
  echo "giysb_staging_bytes=$GIYSB_STAGING_BYTES_VALUE"
  echo "giysb_tiny_table_bytes=$GIYSB_TINY_TABLE_BYTES_VALUE"
  echo "giysb_tiny_object_max_bytes=$GIYSB_TINY_OBJECT_MAX_BYTES_VALUE"
  echo "giysb_tiny_staging_min_bytes=$GIYSB_TINY_STAGING_MIN_BYTES_VALUE"
  echo "configs=${CONFIGS[*]}"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "bench_cpu=${BENCH_CPU:-0}"
  echo "started_at=$(date -Is)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed 's/^/lscpu: /'
  fi
} > "$OUT_BASE/run.info"

build_config() {
  local config="$1"
  local config_dir="$OUT_BASE/$config"
  mkdir -p "$config_dir"

  echo "[$(date -Is)] build $config"
  (
    cd "$BUILD_DIR" || exit 1
    make OPT_GC=giy \
      CACHE_SIZE_KB="$CACHE_SIZE_KB_VALUE" \
      GIY_RSET_READ_SLOT_AT_GC=true \
      GIY_GC_STACK_BYTES="$GIY_GC_STACK_BYTES_VALUE" \
      GIY_SB=true \
      GIYSB_STAGING_BYTES="$GIYSB_STAGING_BYTES_VALUE" \
      GIYSB_TINY_TABLE_BYTES="$GIYSB_TINY_TABLE_BYTES_VALUE" \
      GIYSB_TINY_OBJECT_MAX_BYTES="$GIYSB_TINY_OBJECT_MAX_BYTES_VALUE" \
      GIYSB_TINY_STAGING_MIN_BYTES="$GIYSB_TINY_STAGING_MIN_BYTES_VALUE" \
      -B -j2
  ) > "$config_dir/build.log" 2>&1

  local build_status=$?
  echo "$build_status" > "$config_dir/build.status"
  if [[ "$build_status" != "0" ]]; then
    echo "[$(date -Is)] build failed config=$config status=$build_status"
    return "$build_status"
  fi
  cp "$BUILD_DIR/ejsvm" "$config_dir/ejsvm_$config"
}

run_benchmark() {
  local config="$1"
  local bench="$2"
  local config_dir="$OUT_BASE/$config"
  local out_file="$config_dir/${bench}.out"
  local time_file="$config_dir/${bench}.time"
  local status_file="$config_dir/${bench}.status"

  if [[ -f "$status_file" ]]; then
    local old_status
    old_status="$(cat "$status_file" 2>/dev/null || true)"
    if [[ "$old_status" == "0" ]]; then
      echo "[$(date -Is)] skip config=$config bench=$bench status=0"
      return 0
    fi
  fi

  echo "[$(date -Is)] run config=$config bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    if command -v taskset >/dev/null 2>&1; then
      /usr/bin/time -p taskset -c "${BENCH_CPU:-0}" "$config_dir/ejsvm_$config" "${bench}.sbc" > "$out_file" 2> "$time_file"
    else
      /usr/bin/time -p "$config_dir/ejsvm_$config" "${bench}.sbc" > "$out_file" 2> "$time_file"
    fi
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done config=$config bench=$bench status=$status"
  return 0
}

for config in "${CONFIGS[@]}"; do
  build_config "$config" || exit $?
  for bench in "${BENCHMARKS[@]}"; do
    if [[ ! -f "$BENCH_DIR/${bench}.sbc" ]]; then
      echo "[$(date -Is)] missing benchmark $BENCH_DIR/${bench}.sbc"
      echo "missing" > "$OUT_BASE/$config/${bench}.status"
      continue
    fi
    run_benchmark "$config" "$bench"
  done
done

echo "finished_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
