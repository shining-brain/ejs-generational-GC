#!/usr/bin/env bash
set -u
set -o pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"
OUT_BASE="${1:-$BENCH_DIR/out_gc_cache_miss_$(date +%Y%m%d_%H%M%S)}"

GCS=(cache_cheney giy)
SIZES=(640 768 896)
BENCHMARKS=(
  CD
  Havlak
  Richards
  Storage
)
EVENT_GROUPS=(
  generic
  l1d_load
  llc_load
)

events_for_group() {
  case "$1" in
    generic)
      printf '%s\n' 'cycles,instructions,cache-references,cache-misses'
      ;;
    l1d_load)
      printf '%s\n' 'L1-dcache-loads,L1-dcache-load-misses'
      ;;
    llc_load)
      printf '%s\n' 'LLC-loads,LLC-load-misses'
      ;;
    llc_store)
      printf '%s\n' 'LLC-stores,LLC-store-misses'
      ;;
    *)
      echo "unknown event group: $1" >&2
      return 2
      ;;
  esac
}

mkdir -p "$OUT_BASE"

{
  echo "root=$ROOT"
  echo "build_dir=$BUILD_DIR"
  echo "bench_dir=$BENCH_DIR"
  echo "out_base=$OUT_BASE"
  echo "gcs=${GCS[*]}"
  echo "sizes=${SIZES[*]}"
  echo "benchmarks=${BENCHMARKS[*]}"
  echo "event_groups=${EVENT_GROUPS[*]}"
  for group in "${EVENT_GROUPS[@]}"; do
    echo "events_${group}=$(events_for_group "$group")"
  done
  echo "bench_cpu=${BENCH_CPU:-0}"
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

run_perf_benchmark() {
  local gc="$1"
  local size="$2"
  local bench="$3"
  local group="$4"
  local events
  events="$(events_for_group "$group")" || return 2

  local group_dir="$OUT_BASE/$gc/cache_${size}/$group"
  local out_file="$group_dir/${bench}.out"
  local stderr_file="$group_dir/${bench}.stderr"
  local perf_file="$group_dir/${bench}.perf.csv"
  local status_file="$group_dir/${bench}.status"

  mkdir -p "$group_dir"

  if [[ -f "$status_file" ]]; then
    local old_status
    old_status="$(cat "$status_file" 2>/dev/null || true)"
    if [[ "$old_status" == "0" ]]; then
      echo "[$(date -Is)] skip gc=$gc CACHE_SIZE_KB=$size group=$group bench=$bench status=0"
      return 0
    fi
  fi

  echo "[$(date -Is)] run gc=$gc CACHE_SIZE_KB=$size group=$group bench=$bench"
  (
    cd "$BENCH_DIR" || exit 1
    perf stat -x, -o "$perf_file" -e "$events" -- \
      taskset -c "${BENCH_CPU:-0}" ../ejsvm "${bench}.sbc" \
      > "$out_file" 2> "$stderr_file"
  )
  local status=$?
  echo "$status" > "$status_file"
  echo "[$(date -Is)] done gc=$gc CACHE_SIZE_KB=$size group=$group bench=$bench status=$status"
  return 0
}

for gc in "${GCS[@]}"; do
  for size in "${SIZES[@]}"; do
    config_dir="$OUT_BASE/$gc/cache_${size}"
    mkdir -p "$config_dir"

    echo "[$(date -Is)] build gc=$gc CACHE_SIZE_KB=$size"
    (
      cd "$BUILD_DIR" || exit 1
      make OPT_GC="$gc" CACHE_SIZE_KB="$size" -B -j2
    ) > "$config_dir/build.log" 2>&1
    build_status=$?
    echo "$build_status" > "$config_dir/build.status"
    if [[ "$build_status" != "0" ]]; then
      echo "[$(date -Is)] build failed gc=$gc CACHE_SIZE_KB=$size status=$build_status"
      exit "$build_status"
    fi
    cp "$BUILD_DIR/ejsvm" "$config_dir/ejsvm_${gc}_cache_${size}"

    for group in "${EVENT_GROUPS[@]}"; do
      for bench in "${BENCHMARKS[@]}"; do
        if [[ ! -f "$BENCH_DIR/${bench}.sbc" ]]; then
          echo "[$(date -Is)] missing benchmark $BENCH_DIR/${bench}.sbc"
          mkdir -p "$OUT_BASE/$gc/cache_${size}/$group"
          echo "missing" > "$OUT_BASE/$gc/cache_${size}/$group/${bench}.status"
          continue
        fi
        run_perf_benchmark "$gc" "$size" "$bench" "$group"
      done
    done
  done
done

echo "finished_at=$(date -Is)" >> "$OUT_BASE/run.info"
echo "[$(date -Is)] all done OUT_BASE=$OUT_BASE"
