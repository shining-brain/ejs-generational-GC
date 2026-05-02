#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build.debug"
BENCH_DIR="$BUILD_DIR/benchmarks"

BENCHMARKS="${BENCHMARKS:-Bounce List Sieve Queens Permute Storage Towers Mandelbrot Richards CD NBody Havlak}"
OUT_DIR="${OUT_DIR:-out28_alloc_size_profile}"
JOBS="${JOBS:-2}"

rm -f "$BUILD_DIR/giy_dram_manager.cc" \
      "$BUILD_DIR/giy_dram_manager.o" \
      "$BUILD_DIR/ejsvm"
make -C "$BUILD_DIR" OPT_GC=giy ALLOC_SIZE_PROFILE=true -j"$JOBS"

rm -rf "$BENCH_DIR/$OUT_DIR"
mkdir -p "$BENCH_DIR/$OUT_DIR"

cat > "$BENCH_DIR/$OUT_DIR/README.md" <<EOF
# Allocation size profile

Purpose:
- Collect object allocation size distribution for the benchmark suite.
- Build: \`make OPT_GC=giy ALLOC_SIZE_PROFILE=true -j$JOBS\`.
- Payload size means \`request_bytes\` passed to \`space_alloc\`.
- Footprint size means \`ALIGN(payload + object_header)\`, which is the size relevant to GiY materialization copy.

Benchmarks:
\`$BENCHMARKS\`

Output:
- One \`<benchmark>.out\` file per benchmark with the full allocation histograms.
- \`summary.tsv\` with \`<=256\` and \`>256\` allocation counts for payload and footprint.
EOF

summary="$BENCH_DIR/$OUT_DIR/summary.tsv"
printf "benchmark\ttotal_allocs\tavg_payload\tavg_footprint\tpayload_le256\tpayload_le256_pct\tpayload_gt256\tpayload_gt256_pct\tfootprint_le256\tfootprint_le256_pct\tfootprint_gt256\tfootprint_gt256_pct\n" > "$summary"

for bench in $BENCHMARKS; do
  echo "====== Running allocation profile: $bench ======"
  (
    cd "$BENCH_DIR"
    { time -p ../ejsvm "$bench.sbc"; } > "$OUT_DIR/$bench.out" 2>&1
  )

  awk -v bench="$bench" '
    /^Total profiled allocs:/ { total = $4 }
    /^Avg payload size:/ { avg_payload = $4 }
    /^Avg footprint size:/ { avg_footprint = $4 }
    /^Payload histogram:/ { mode = "payload"; next }
    /^Footprint histogram:/ { mode = "footprint"; next }
    /^Allocation by cell type:/ { mode = ""; next }
    ($1 == "payload" || $1 == "footprint") {
      upper = $2
      if (upper ~ /^<=/) {
        sub(/^<=/, "", upper)
      } else if (upper ~ /^>/) {
        upper = 999999999
      } else if (upper ~ /-/) {
        split(upper, parts, "-")
        upper = parts[2]
      }
      upper += 0

      if (mode == "payload") {
        if (upper <= 256) payload_le += $3
        else payload_gt += $3
      } else if (mode == "footprint") {
        if (upper <= 256) footprint_le += $3
        else footprint_gt += $3
      }
    }
    END {
      if (total == 0) total = 1
      printf "%s\t%s\t%s\t%s\t%.0f\t%.2f\t%.0f\t%.2f\t%.0f\t%.2f\t%.0f\t%.2f\n",
             bench, total, avg_payload, avg_footprint,
             payload_le, 100.0 * payload_le / total,
             payload_gt, 100.0 * payload_gt / total,
             footprint_le, 100.0 * footprint_le / total,
             footprint_gt, 100.0 * footprint_gt / total
    }
  ' "$BENCH_DIR/$OUT_DIR/$bench.out" >> "$summary"

  echo "Finished $bench"
  echo ""
done

echo "Allocation size profile completed: $BENCH_DIR/$OUT_DIR"
echo "Summary: $summary"
