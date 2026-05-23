#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path


DEFAULT_GCS = ["cache_cheney", "giy"]
DEFAULT_SIZES = [640, 768, 896]
DEFAULT_BENCHMARKS = ["CD", "Havlak", "Richards", "Storage"]
DEFAULT_GROUPS = ["generic", "l1d_load", "llc_load"]


OUT_PATTERNS = {
    "total_execution_sec": re.compile(r"Total execution:\s+([0-9.]+)\s+sec"),
    "gc_overhead_sec": re.compile(r"GC overhead\(full\):\s+([0-9.]+)\s+sec"),
    "total_cpu_sec": re.compile(r"Total CPU time:\s+([0-9.]+)\s+sec"),
    "gc_cpu_sec": re.compile(r"GC CPU\(full\):\s+([0-9.]+)\s+sec"),
    "minor_gc_count": re.compile(r"Minor GC count:\s+([0-9]+)"),
    "avg_gc_pause_ms": re.compile(r"Avg GC pause\(full\):\s+([0-9.]+)\s+ms"),
    "total_alloc_bytes_mb": re.compile(r"Total alloc bytes:\s+([0-9.]+)\s+MB"),
    "forward_operations": re.compile(r"Forward operations:\s+([0-9]+)"),
    "gc_pmu_sampled": re.compile(r"PMU-sampled GC:\s+([0-9]+)\s*/\s*([0-9]+)"),
    "gc_cache_misses": re.compile(r"cache-misses:\s+([0-9]+)"),
    "gc_cache_references": re.compile(r"cache-references:\s+([0-9]+)"),
    "gc_cache_miss_rate_pct": re.compile(r"cache-miss rate:\s+([0-9.]+)%"),
    "gc_instructions": re.compile(r"instructions:\s+([0-9]+)"),
    "gc_cache_mpki": re.compile(r"cache MPKI:\s+([0-9.]+)"),
}


def parse_float(value):
    if value is None or value == "":
        return ""
    return float(value)


def parse_int(value):
    if value is None or value == "":
        return ""
    return int(value)


def read_status(path):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return ""


def parse_run_info(path):
    info = {}
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        return info
    for line in text.splitlines():
        if "=" not in line or line.startswith("lscpu:") or line.startswith("perf:"):
            continue
        key, value = line.split("=", 1)
        info[key.strip()] = value.strip()
    return info


def parse_int_list(value, default):
    if not value:
        return default
    result = []
    for item in value.split():
        try:
            result.append(int(item))
        except ValueError:
            return default
    return result or default


def parse_str_list(value, default):
    if not value:
        return default
    result = value.split()
    return result or default


def parse_out(path):
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        text = ""
    row = {}
    for key, pattern in OUT_PATTERNS.items():
        match = pattern.search(text)
        if key == "gc_pmu_sampled":
            row["gc_pmu_sampled_count"] = parse_int(match.group(1)) if match else ""
            row["gc_pmu_total_gc"] = parse_int(match.group(2)) if match else ""
            continue
        value = match.group(1) if match else ""
        if key in (
            "minor_gc_count",
            "forward_operations",
            "gc_cache_misses",
            "gc_cache_references",
            "gc_instructions",
        ):
            row[key] = parse_int(value)
        else:
            row[key] = parse_float(value)
    return row


def parse_perf(path):
    events = {}
    try:
        lines = path.read_text(errors="replace").splitlines()
    except FileNotFoundError:
        lines = []
    for line in lines:
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) < 3:
            continue
        raw_value = parts[0].strip()
        event = parts[2].strip()
        running_pct = parts[4].strip() if len(parts) > 4 else ""
        not_counted = raw_value == "<not counted>"
        value = "" if not_counted else parse_float(raw_value)
        events[event] = {
            "value": value,
            "not_counted": not_counted,
            "running_pct": parse_float(running_pct) if running_pct else "",
        }
    return events


def event_value(events, name):
    return events.get(name, {}).get("value", "")


def event_not_counted(events, name):
    return events.get(name, {}).get("not_counted", "")


def safe_rate(num, den):
    if num == "" or den == "" or den == 0:
        return ""
    return 100.0 * num / den


def safe_mpki(misses, instructions):
    if misses == "" or instructions == "" or instructions == 0:
        return ""
    return 1000.0 * misses / instructions


def fmt(value, digits=3):
    if value == "" or value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def main():
    if len(sys.argv) != 2:
        print("usage: parse_gc_cache_miss_benchmarks.py OUT_BASE", file=sys.stderr)
        return 2

    out_base = Path(sys.argv[1]).resolve()
    info = parse_run_info(out_base / "run.info")
    gcs = parse_str_list(info.get("gcs", ""), DEFAULT_GCS)
    sizes = parse_int_list(info.get("sizes", ""), DEFAULT_SIZES)
    benchmarks = parse_str_list(info.get("benchmarks", ""), DEFAULT_BENCHMARKS)
    groups = parse_str_list(info.get("event_groups", info.get("groups", "")), DEFAULT_GROUPS)

    rows = []
    for gc in gcs:
        for size in sizes:
            for group in groups:
                group_dir = out_base / gc / f"cache_{size}" / group
                for bench in benchmarks:
                    events = parse_perf(group_dir / f"{bench}.perf.csv")
                    row = {
                        "gc": gc,
                        "cache_size_kb": size,
                        "event_group": group,
                        "benchmark": bench,
                        "status": read_status(group_dir / f"{bench}.status"),
                    }
                    row.update(parse_out(group_dir / f"{bench}.out"))

                    row["perf_cycles"] = event_value(events, "cycles")
                    row["perf_instructions"] = event_value(events, "instructions")
                    row["perf_cache_references"] = event_value(events, "cache-references")
                    row["perf_cache_misses"] = event_value(events, "cache-misses")
                    row["perf_cache_miss_rate_pct"] = safe_rate(
                        row["perf_cache_misses"], row["perf_cache_references"]
                    )
                    row["perf_cache_mpki"] = safe_mpki(
                        row["perf_cache_misses"], row["perf_instructions"]
                    )

                    row["perf_l1d_loads"] = event_value(events, "L1-dcache-loads")
                    row["perf_l1d_load_misses"] = event_value(events, "L1-dcache-load-misses")
                    row["perf_l1d_load_miss_rate_pct"] = safe_rate(
                        row["perf_l1d_load_misses"], row["perf_l1d_loads"]
                    )

                    row["perf_llc_loads"] = event_value(events, "LLC-loads")
                    row["perf_llc_load_misses"] = event_value(events, "LLC-load-misses")
                    row["perf_llc_load_miss_rate_pct"] = safe_rate(
                        row["perf_llc_load_misses"], row["perf_llc_loads"]
                    )

                    row["not_counted_cycles"] = event_not_counted(events, "cycles")
                    row["not_counted_instructions"] = event_not_counted(events, "instructions")
                    row["not_counted_cache_references"] = event_not_counted(events, "cache-references")
                    row["not_counted_cache_misses"] = event_not_counted(events, "cache-misses")
                    row["not_counted_l1d_loads"] = event_not_counted(events, "L1-dcache-loads")
                    row["not_counted_l1d_load_misses"] = event_not_counted(events, "L1-dcache-load-misses")
                    row["not_counted_llc_loads"] = event_not_counted(events, "LLC-loads")
                    row["not_counted_llc_load_misses"] = event_not_counted(events, "LLC-load-misses")
                    rows.append(row)

    csv_path = out_base / "summary.csv"
    fieldnames = [
        "gc",
        "cache_size_kb",
        "event_group",
        "benchmark",
        "status",
        "total_execution_sec",
        "gc_overhead_sec",
        "total_cpu_sec",
        "gc_cpu_sec",
        "minor_gc_count",
        "avg_gc_pause_ms",
        "total_alloc_bytes_mb",
        "forward_operations",
        "gc_pmu_sampled_count",
        "gc_pmu_total_gc",
        "gc_cache_misses",
        "gc_cache_references",
        "gc_cache_miss_rate_pct",
        "gc_instructions",
        "gc_cache_mpki",
        "perf_cycles",
        "perf_instructions",
        "perf_cache_references",
        "perf_cache_misses",
        "perf_cache_miss_rate_pct",
        "perf_cache_mpki",
        "perf_l1d_loads",
        "perf_l1d_load_misses",
        "perf_l1d_load_miss_rate_pct",
        "perf_llc_loads",
        "perf_llc_load_misses",
        "perf_llc_load_miss_rate_pct",
        "not_counted_cycles",
        "not_counted_instructions",
        "not_counted_cache_references",
        "not_counted_cache_misses",
        "not_counted_l1d_loads",
        "not_counted_l1d_load_misses",
        "not_counted_llc_loads",
        "not_counted_llc_load_misses",
    ]
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    md_path = out_base / "summary.md"
    with md_path.open("w") as f:
        f.write("# GC Cache Miss Benchmark Summary\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write("## Completed Runs\n\n")
        f.write("| GC | Cache KB | Group | Completed |\n")
        f.write("|---|---:|---|---:|\n")
        for gc in gcs:
            for size in sizes:
                for group in groups:
                    subset = [
                        r for r in rows
                        if r["gc"] == gc and r["cache_size_kb"] == size and r["event_group"] == group
                    ]
                    completed = sum(1 for r in subset if r["status"] == "0")
                    f.write(f"| {gc} | {size} | {group} | {completed}/{len(subset)} |\n")

        f.write("\n## Aggregate Perf Counters\n\n")
        f.write("| GC | Cache KB | Group | Total time(s) | Miss metric | Misses | References/Loads | Miss rate | MPKI |\n")
        f.write("|---|---:|---|---:|---|---:|---:|---:|---:|\n")
        for gc in gcs:
            for size in sizes:
                for group in groups:
                    subset = [
                        r for r in rows
                        if r["gc"] == gc and r["cache_size_kb"] == size and
                        r["event_group"] == group and r["status"] == "0"
                    ]
                    total_time = sum(r["total_execution_sec"] for r in subset if r["total_execution_sec"] != "")
                    metric = ""
                    misses = refs = instr = ""
                    if group == "generic":
                        metric = "cache"
                        misses = sum(r["perf_cache_misses"] for r in subset if r["perf_cache_misses"] != "")
                        refs = sum(r["perf_cache_references"] for r in subset if r["perf_cache_references"] != "")
                        instr = sum(r["perf_instructions"] for r in subset if r["perf_instructions"] != "")
                    elif group == "l1d_load":
                        metric = "L1D load"
                        misses = sum(r["perf_l1d_load_misses"] for r in subset if r["perf_l1d_load_misses"] != "")
                        refs = sum(r["perf_l1d_loads"] for r in subset if r["perf_l1d_loads"] != "")
                    elif group == "llc_load":
                        metric = "LLC load"
                        misses = sum(r["perf_llc_load_misses"] for r in subset if r["perf_llc_load_misses"] != "")
                        refs = sum(r["perf_llc_loads"] for r in subset if r["perf_llc_loads"] != "")
                    miss_rate = safe_rate(misses, refs)
                    mpki = safe_mpki(misses, instr) if group == "generic" else ""
                    f.write(
                        f"| {gc} | {size} | {group} | {fmt(total_time)} | {metric} | "
                        f"{fmt(misses, 0)} | {fmt(refs, 0)} | {fmt(miss_rate)} | {fmt(mpki)} |\n"
                    )

        f.write("\n## GC-Window PMU From Generic Runs\n\n")
        f.write("| GC | Cache KB | Total time(s) | GC overhead(s) | GC misses | GC refs | GC miss rate | GC MPKI | Minor GC |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for gc in gcs:
            for size in sizes:
                subset = [
                    r for r in rows
                    if r["gc"] == gc and r["cache_size_kb"] == size and
                    r["event_group"] == "generic" and r["status"] == "0"
                ]
                total_time = sum(r["total_execution_sec"] for r in subset if r["total_execution_sec"] != "")
                gc_overhead = sum(r["gc_overhead_sec"] for r in subset if r["gc_overhead_sec"] != "")
                misses = sum(r["gc_cache_misses"] for r in subset if r["gc_cache_misses"] != "")
                refs = sum(r["gc_cache_references"] for r in subset if r["gc_cache_references"] != "")
                instr = sum(r["gc_instructions"] for r in subset if r["gc_instructions"] != "")
                minor = sum(r["minor_gc_count"] for r in subset if r["minor_gc_count"] != "")
                miss_rate = safe_rate(misses, refs)
                mpki = safe_mpki(misses, instr)
                f.write(
                    f"| {gc} | {size} | {fmt(total_time)} | {fmt(gc_overhead)} | "
                    f"{fmt(misses, 0)} | {fmt(refs, 0)} | {fmt(miss_rate)} | {fmt(mpki)} | {fmt(minor, 0)} |\n"
                )

    print(csv_path)
    print(md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
