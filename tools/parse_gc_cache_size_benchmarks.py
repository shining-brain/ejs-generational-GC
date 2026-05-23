#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path


DEFAULT_SIZES = [512, 640, 768, 896]
DEFAULT_BENCHMARKS = [
    "Bounce",
    "CD",
    "DeltaBlue",
    "Havlak",
    "List",
    "Mandelbrot",
    "NBody",
    "Permute",
    "Queens",
    "Richards",
    "Sieve",
    "Storage",
    "Towers",
]


PATTERNS = {
    "workspace_after_rset_kb": re.compile(r"total_size=([0-9]+)KB"),
    "cache_size_kb_reported": re.compile(r"Cache size\s+([0-9]+)\s+Kbytes"),
    "total_execution_sec": re.compile(r"Total execution:\s+([0-9.]+)\s+sec"),
    "gc_overhead_sec": re.compile(r"GC overhead\(full\):\s+([0-9.]+)\s+sec"),
    "total_cpu_sec": re.compile(r"Total CPU time:\s+([0-9.]+)\s+sec"),
    "gc_cpu_sec": re.compile(r"GC CPU\(full\):\s+([0-9.]+)\s+sec"),
    "minor_gc_count": re.compile(r"Minor GC count:\s+([0-9]+)"),
    "avg_gc_pause_ms": re.compile(r"Avg GC pause\(full\):\s+([0-9.]+)\s+ms"),
    "total_alloc_bytes_mb": re.compile(r"Total alloc bytes:\s+([0-9.]+)\s+MB"),
    "forward_operations": re.compile(r"Forward operations:\s+([0-9]+)"),
    "young_before_aux_kb": re.compile(r"Young before aux:\s+([0-9.]+)\s+KB"),
    "young_after_aux_kb": re.compile(r"Young after aux:\s+([0-9.]+)\s+KB"),
    "aux_total_kb": re.compile(r"Aux total:\s+([0-9.]+)\s+KB"),
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
        if "=" not in line or line.startswith("lscpu:"):
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


def parse_time(path):
    result = {"time_real_sec": "", "time_user_sec": "", "time_sys_sec": ""}
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        return result
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in ("real", "user", "sys"):
            result[f"time_{parts[0]}_sec"] = parse_float(parts[1])
    return result


def parse_out(path):
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        text = ""
    row = {}
    for key, pattern in PATTERNS.items():
        match = pattern.search(text)
        value = match.group(1) if match else ""
        if key in (
            "workspace_after_rset_kb",
            "cache_size_kb_reported",
            "minor_gc_count",
            "forward_operations",
        ):
            row[key] = parse_int(value)
        else:
            row[key] = parse_float(value)
    return row


def fmt(value, digits=3):
    if value == "" or value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def main():
    if len(sys.argv) != 2:
        print("usage: parse_gc_cache_size_benchmarks.py OUT_BASE", file=sys.stderr)
        return 2
    out_base = Path(sys.argv[1]).resolve()
    run_info = parse_run_info(out_base / "run.info")
    sizes = parse_int_list(run_info.get("sizes", ""), DEFAULT_SIZES)
    benchmarks = parse_str_list(run_info.get("benchmarks", ""), DEFAULT_BENCHMARKS)
    gc_name = run_info.get("gc", "unknown")

    rows = []
    for size in sizes:
        size_dir = out_base / f"cache_{size}"
        for bench in benchmarks:
            row = {
                "gc": gc_name,
                "cache_size_kb": size,
                "benchmark": bench,
                "status": read_status(size_dir / f"{bench}.status"),
            }
            row.update(parse_out(size_dir / f"{bench}.out"))
            row.update(parse_time(size_dir / f"{bench}.time"))
            rows.append(row)

    csv_path = out_base / "summary.csv"
    fieldnames = [
        "gc",
        "cache_size_kb",
        "benchmark",
        "status",
        "workspace_after_rset_kb",
        "cache_size_kb_reported",
        "total_execution_sec",
        "time_real_sec",
        "total_cpu_sec",
        "gc_overhead_sec",
        "gc_cpu_sec",
        "minor_gc_count",
        "avg_gc_pause_ms",
        "total_alloc_bytes_mb",
        "forward_operations",
        "young_before_aux_kb",
        "young_after_aux_kb",
        "aux_total_kb",
        "time_user_sec",
        "time_sys_sec",
    ]
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)

    md_path = out_base / "summary.md"
    with md_path.open("w") as f:
        f.write(f"# {gc_name} Cache Size Benchmark Summary\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write("## Per Benchmark Runtime\n\n")
        f.write("| Benchmark | " + " | ".join(f"{size}s" for size in sizes) + " | Best |\n")
        f.write("|---|" + "|".join("---:" for _ in sizes) + "|---|\n")
        by_bench = {bench: {} for bench in benchmarks}
        for row in rows:
            by_bench[row["benchmark"]][row["cache_size_kb"]] = row
        for bench in benchmarks:
            vals = []
            best_size = ""
            best_time = None
            for size in sizes:
                row = by_bench[bench].get(size, {})
                t = row.get("total_execution_sec", "")
                vals.append(fmt(t))
                if t != "" and (best_time is None or t < best_time):
                    best_time = t
                    best_size = size
            f.write(f"| {bench} | " + " | ".join(vals) + f" | {best_size} |\n")

        f.write("\n## Minor GC Count\n\n")
        f.write("| Benchmark | " + " | ".join(str(size) for size in sizes) + " |\n")
        f.write("|---|" + "|".join("---:" for _ in sizes) + "|\n")
        for bench in benchmarks:
            vals = []
            for size in sizes:
                row = by_bench[bench].get(size, {})
                vals.append(fmt(row.get("minor_gc_count", "")))
            f.write(f"| {bench} | " + " | ".join(vals) + " |\n")

        successful = [r for r in rows if r.get("status") == "0" and r.get("total_execution_sec") != ""]
        f.write("\n## Aggregate\n\n")
        f.write("| Cache KB | Completed | Sum total execution sec | Sum GC overhead sec | Sum minor GCs |\n")
        f.write("|---:|---:|---:|---:|---:|\n")
        for size in sizes:
            subset = [r for r in successful if r["cache_size_kb"] == size]
            total = sum(r["total_execution_sec"] for r in subset)
            gc = sum(r["gc_overhead_sec"] for r in subset if r["gc_overhead_sec"] != "")
            minor = sum(r["minor_gc_count"] for r in subset if r["minor_gc_count"] != "")
            f.write(f"| {size} | {len(subset)} | {total:.3f} | {gc:.3f} | {minor} |\n")

    print(csv_path)
    print(md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
