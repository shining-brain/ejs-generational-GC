#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"
OUT_BASE="${1:-$BENCH_DIR/out_giy_force_nt_young532_$(date +%Y%m%d_%H%M%S)}"
case "$OUT_BASE" in
  /*) ;;
  *) OUT_BASE="$ROOT/$OUT_BASE" ;;
esac

CACHE_SIZE_KB_VALUE=896
GIY_GC_STACK_BYTES_VALUE=49152
GIY_LOCAL_PADDING_BYTES_VALUE=73728
GIY_NT_COPY_BITS_VALUE=256
GIY_OLD_SLOT_NT_STORE_VALUE=0
GIY_FORCE_NT_COPY_VALUE=1

CONFIG="giy_force_nt_young532_896"

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

mkdir -p "$OUT_BASE/smoke"

{
  echo "root=$ROOT"
  echo "build_dir=$BUILD_DIR"
  echo "bench_dir=$BENCH_DIR"
  echo "out_base=$OUT_BASE"
  echo "cache_size_kb=$CACHE_SIZE_KB_VALUE"
  echo "giy_gc_stack_bytes=$GIY_GC_STACK_BYTES_VALUE"
  echo "giy_local_padding_bytes=$GIY_LOCAL_PADDING_BYTES_VALUE"
  echo "giy_nt_copy_bits=$GIY_NT_COPY_BITS_VALUE"
  echo "giy_old_slot_nt_store=$GIY_OLD_SLOT_NT_STORE_VALUE"
  echo "giy_force_nt_copy=$GIY_FORCE_NT_COPY_VALUE"
  echo "pmu_disabled=1"
  echo "configs=$CONFIG"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "bench_cpu=${BENCH_CPU:-0}"
  echo "started_at=$(date -Is)"
  if command -v lscpu >/dev/null 2>&1; then
    lscpu | sed 's/^/lscpu: /'
  fi
} > "$OUT_BASE/run.info"

CONFIG_DIR="$OUT_BASE/$CONFIG"
mkdir -p "$CONFIG_DIR"

echo "[$(date -Is)] build $CONFIG"
(
  cd "$BUILD_DIR" || exit 1
  make OPT_GC=giy \
    CACHE_SIZE_KB="$CACHE_SIZE_KB_VALUE" \
    GIY_RSET_READ_SLOT_AT_GC=true \
    GIY_GC_STACK_BYTES="$GIY_GC_STACK_BYTES_VALUE" \
    GIY_LOCAL_PADDING_BYTES="$GIY_LOCAL_PADDING_BYTES_VALUE" \
    GIY_NT_COPY_BITS="$GIY_NT_COPY_BITS_VALUE" \
    GIY_OLD_SLOT_NT_STORE="$GIY_OLD_SLOT_NT_STORE_VALUE" \
    GIY_FORCE_NT_COPY="$GIY_FORCE_NT_COPY_VALUE" \
    -B -j2
) > "$CONFIG_DIR/build.log" 2>&1
BUILD_STATUS=$?
echo "$BUILD_STATUS" > "$CONFIG_DIR/build.status"
if [[ "$BUILD_STATUS" != "0" ]]; then
  echo "[$(date -Is)] build failed config=$CONFIG status=$BUILD_STATUS"
  exit "$BUILD_STATUS"
fi
cp "$BUILD_DIR/ejsvm" "$CONFIG_DIR/ejsvm_$CONFIG"

run_program() {
  local bench="$1"
  local out_file="$CONFIG_DIR/${bench}.out"
  local time_file="$CONFIG_DIR/${bench}.time"
  local status_file="$CONFIG_DIR/${bench}.status"

  if [[ -f "$status_file" ]]; then
    local old_status
    old_status="$(cat "$status_file" 2>/dev/null || true)"
    if [[ "$old_status" == "0" ]]; then
      echo "[$(date -Is)] skip config=$CONFIG bench=$bench status=0"
      return 0
    fi
  fi

  echo "[$(date -Is)] run config=$CONFIG bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    if command -v taskset >/dev/null 2>&1; then
      EJS_DISABLE_GC_PMU=1 /usr/bin/time -p taskset -c "${BENCH_CPU:-0}" "$CONFIG_DIR/ejsvm_$CONFIG" "${bench}.sbc" > "$out_file" 2> "$time_file"
    else
      EJS_DISABLE_GC_PMU=1 /usr/bin/time -p "$CONFIG_DIR/ejsvm_$CONFIG" "${bench}.sbc" > "$out_file" 2> "$time_file"
    fi
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done config=$CONFIG bench=$bench status=$status"
  return "$status"
}

run_smoke() {
  local bench="giy_gc_probe"
  local out_file="$OUT_BASE/smoke/${CONFIG}_${bench}.out"
  local time_file="$OUT_BASE/smoke/${CONFIG}_${bench}.time"
  local status_file="$OUT_BASE/smoke/${CONFIG}_${bench}.status"

  echo "[$(date -Is)] smoke config=$CONFIG bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    if command -v taskset >/dev/null 2>&1; then
      EJS_DISABLE_GC_PMU=1 /usr/bin/time -p taskset -c "${BENCH_CPU:-0}" "$CONFIG_DIR/ejsvm_$CONFIG" "${bench}.sbc" > "$out_file" 2> "$time_file"
    else
      EJS_DISABLE_GC_PMU=1 /usr/bin/time -p "$CONFIG_DIR/ejsvm_$CONFIG" "${bench}.sbc" > "$out_file" 2> "$time_file"
    fi
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] smoke done config=$CONFIG bench=$bench status=$status"
  return "$status"
}

run_smoke || exit $?

for bench in "${BENCHMARKS[@]}"; do
  run_program "$bench" || exit $?
done

echo "ended_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
