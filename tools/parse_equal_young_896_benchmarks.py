#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path


DEFAULT_CONFIGS = ["cheney896_equal_young", "giy896"]
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
    "total_cpu_sec": re.compile(r"Total CPU time:\s+([0-9.]+)\s+sec"),
    "gc_overhead_sec": re.compile(r"GC overhead\(full\):\s+([0-9.]+)\s+sec"),
    "gc_cpu_sec": re.compile(r"GC CPU\(full\):\s+([0-9.]+)\s+sec"),
    "minor_gc_count": re.compile(r"Minor GC count:\s+([0-9]+)"),
    "avg_gc_pause_ms": re.compile(r"Avg GC pause\(full\):\s+([0-9.]+)\s+ms"),
    "write_barrier_calls": re.compile(r"Write barrier calls:\s+([0-9]+)"),
    "total_alloc_bytes_mb": re.compile(r"Total alloc bytes:\s+([0-9.]+)\s+MB"),
    "forward_operations": re.compile(r"Forward operations:\s+([0-9]+)"),
    "young_before_aux_kb": re.compile(r"Young before aux:\s+([0-9.]+)\s+KB"),
    "young_after_aux_kb": re.compile(r"Young after aux:\s+([0-9.]+)\s+KB"),
    "aux_total_kb": re.compile(r"Aux total:\s+([0-9.]+)\s+KB"),
    "gc_core_sec": re.compile(r"GC core total:\s+([0-9.]+)\s+sec"),
    "scan_roots_sec": re.compile(r"scan_roots:\s+([0-9.]+)\s+sec"),
    "scan_rs_sec": re.compile(r"scan_RS:\s+([0-9.]+)\s+sec"),
    "scavenge_sec": re.compile(r"scavenge:\s+([0-9.]+)\s+sec"),
    "gc_cache_misses": re.compile(r"cache-misses:\s+([0-9]+)"),
    "gc_cache_refs": re.compile(r"cache-references:\s+([0-9]+)"),
    "gc_cache_miss_rate_pct": re.compile(r"cache-miss rate:\s+([0-9.]+)%"),
    "gc_instructions": re.compile(r"instructions:\s+([0-9]+)"),
}

INT_KEYS = {
    "workspace_after_rset_kb",
    "cache_size_kb_reported",
    "minor_gc_count",
    "write_barrier_calls",
    "forward_operations",
    "gc_cache_misses",
    "gc_cache_refs",
    "gc_instructions",
}


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


def parse_float(value):
    return "" if value == "" else float(value)


def parse_int(value):
    return "" if value == "" else int(value)


def read_status(path):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return ""


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
        row[key] = parse_int(value) if key in INT_KEYS else parse_float(value)
    total = row.get("total_execution_sec", "")
    gc = row.get("gc_overhead_sec", "")
    row["business_sec"] = total - gc if total != "" and gc != "" else ""
    return row


def fmt(value, digits=3):
    if value == "" or value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def pct(delta, base):
    if delta == "" or base in ("", 0):
        return ""
    return delta / base * 100.0


def sum_key(rows, key):
    vals = [row[key] for row in rows if row.get(key, "") != ""]
    if not vals:
        return ""
    return sum(vals)


def main():
    if len(sys.argv) != 2:
        print("usage: parse_equal_young_896_benchmarks.py OUT_BASE", file=sys.stderr)
        return 2

    out_base = Path(sys.argv[1]).resolve()
    info = parse_run_info(out_base / "run.info")
    configs = info.get("configs", "").split() or DEFAULT_CONFIGS
    benchmarks = info.get("benchmarks", "").split() or DEFAULT_BENCHMARKS

    rows = []
    for config in configs:
        config_dir = out_base / config
        for bench in benchmarks:
            row = {
                "config": config,
                "benchmark": bench,
                "status": read_status(config_dir / f"{bench}.status"),
            }
            row.update(parse_out(config_dir / f"{bench}.out"))
            row.update(parse_time(config_dir / f"{bench}.time"))
            rows.append(row)

    fieldnames = [
        "config",
        "benchmark",
        "status",
        "workspace_after_rset_kb",
        "cache_size_kb_reported",
        "young_before_aux_kb",
        "young_after_aux_kb",
        "aux_total_kb",
        "total_execution_sec",
        "business_sec",
        "gc_overhead_sec",
        "gc_core_sec",
        "scan_roots_sec",
        "scan_rs_sec",
        "scavenge_sec",
        "total_cpu_sec",
        "gc_cpu_sec",
        "minor_gc_count",
        "avg_gc_pause_ms",
        "write_barrier_calls",
        "total_alloc_bytes_mb",
        "forward_operations",
        "gc_cache_misses",
        "gc_cache_refs",
        "gc_cache_miss_rate_pct",
        "gc_instructions",
        "time_real_sec",
        "time_user_sec",
        "time_sys_sec",
    ]

    csv_path = out_base / "summary.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    by_config = {config: {} for config in configs}
    for row in rows:
        by_config.setdefault(row["config"], {})[row["benchmark"]] = row

    cheney_name = configs[0]
    giy_name = configs[1] if len(configs) > 1 else ""
    cheney_rows = [row for row in rows if row["config"] == cheney_name and row["status"] == "0"]
    giy_rows = [row for row in rows if row["config"] == giy_name and row["status"] == "0"]

    report_path = out_base / "compare_equal_young.md"
    with report_path.open("w") as f:
        f.write("# Cheney Equal-Young vs GiY 896KB\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write("Config:\n\n")
        f.write(f"- Cheney: `CACHE_SIZE_KB={info.get('cache_size_kb', '')}`, `CHENEY_LOCAL_PADDING_BYTES={info.get('cheney_local_padding_bytes', '')}`\n")
        f.write(f"- GiY: `CACHE_SIZE_KB={info.get('cache_size_kb', '')}`, `GIY_GC_STACK_BYTES={info.get('giy_gc_stack_bytes', '')}`\n\n")

        metrics = [
            ("Total sec", "total_execution_sec"),
            ("Business sec", "business_sec"),
            ("GC full sec", "gc_overhead_sec"),
            ("GC core sec", "gc_core_sec"),
            ("scan_RS sec", "scan_rs_sec"),
            ("scavenge sec", "scavenge_sec"),
            ("Minor GC count", "minor_gc_count"),
            ("Forward ops", "forward_operations"),
            ("GC cache misses", "gc_cache_misses"),
            ("GC cache refs", "gc_cache_refs"),
        ]

        f.write("## Aggregate\n\n")
        f.write("| Metric | Cheney equal-young | GiY | GiY-Cheney | Delta % |\n")
        f.write("|---|---:|---:|---:|---:|\n")
        for label, key in metrics:
            c = sum_key(cheney_rows, key)
            g = sum_key(giy_rows, key)
            d = g - c if c != "" and g != "" else ""
            f.write(f"| {label} | {fmt(c)} | {fmt(g)} | {fmt(d)} | {fmt(pct(d, c))}% |\n")

        c_misses = sum_key(cheney_rows, "gc_cache_misses")
        c_refs = sum_key(cheney_rows, "gc_cache_refs")
        g_misses = sum_key(giy_rows, "gc_cache_misses")
        g_refs = sum_key(giy_rows, "gc_cache_refs")
        if c_misses != "" and c_refs and g_misses != "" and g_refs:
            c_rate = c_misses / c_refs * 100.0
            g_rate = g_misses / g_refs * 100.0
            f.write(f"| Weighted GC miss rate | {fmt(c_rate)}% | {fmt(g_rate)}% | {fmt(g_rate - c_rate)} pp |  |\n")

        f.write("\n## Workspace Check\n\n")
        f.write("| Config | Young before aux KB | Young after aux KB | Aux total KB |\n")
        f.write("|---|---:|---:|---:|\n")
        for config in configs:
            first = next((row for row in rows if row["config"] == config and row.get("young_after_aux_kb", "") != ""), {})
            f.write(
                f"| {config} | {fmt(first.get('young_before_aux_kb', ''))} | "
                f"{fmt(first.get('young_after_aux_kb', ''))} | "
                f"{fmt(first.get('aux_total_kb', ''))} |\n"
            )

        f.write("\n## Per Benchmark\n\n")
        f.write("| Benchmark | Cheney total | GiY total | Total delta | Total delta % | Cheney GC | GiY GC | GC delta | GC delta % | Cheney minor | GiY minor |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n")
        for bench in benchmarks:
            c = by_config.get(cheney_name, {}).get(bench, {})
            g = by_config.get(giy_name, {}).get(bench, {})
            ct = c.get("total_execution_sec", "")
            gt = g.get("total_execution_sec", "")
            cgc = c.get("gc_overhead_sec", "")
            ggc = g.get("gc_overhead_sec", "")
            dt = gt - ct if ct != "" and gt != "" else ""
            dgc = ggc - cgc if cgc != "" and ggc != "" else ""
            f.write(
                f"| {bench} | {fmt(ct)} | {fmt(gt)} | {fmt(dt)} | {fmt(pct(dt, ct))}% | "
                f"{fmt(cgc)} | {fmt(ggc)} | {fmt(dgc)} | {fmt(pct(dgc, cgc))}% | "
                f"{fmt(c.get('minor_gc_count', ''))} | {fmt(g.get('minor_gc_count', ''))} |\n"
            )

    print(csv_path)
    print(report_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
