#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path


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
    "business_logic_sec": re.compile(r"Business logic:\s+([0-9.]+)\s+sec"),
    "gc_overhead_sec": re.compile(r"GC overhead\(full\):\s+([0-9.]+)\s+sec"),
    "total_cpu_sec": re.compile(r"Total CPU time:\s+([0-9.]+)\s+sec"),
    "business_cpu_sec": re.compile(r"Business CPU:\s+([0-9.]+)\s+sec"),
    "gc_cpu_sec": re.compile(r"GC CPU\(full\):\s+([0-9.]+)\s+sec"),
    "minor_gc_count": re.compile(r"Minor GC count:\s+([0-9]+)"),
    "avg_gc_pause_ms": re.compile(r"Avg GC pause\(full\):\s+([0-9.]+)\s+ms"),
    "write_barrier_calls": re.compile(r"Write barrier calls:\s+([0-9]+)"),
    "write_barrier_duplicates": re.compile(r"Duplicates:\s+([0-9]+)"),
    "total_allocations": re.compile(r"Total allocations:\s+([0-9]+)"),
    "total_alloc_bytes_mb": re.compile(r"Total alloc bytes:\s+([0-9.]+)\s+MB"),
    "forward_operations": re.compile(r"Forward operations:\s+([0-9]+)"),
    "gc_core_sec": re.compile(r"GC core total:\s+([0-9.]+)\s+sec"),
    "scan_roots_sec": re.compile(r"scan_roots:\s+([0-9.]+)\s+sec"),
    "scan_rs_sec": re.compile(r"scan_RS:\s+([0-9.]+)\s+sec"),
    "scavenge_sec": re.compile(r"scavenge:\s+([0-9.]+)\s+sec"),
    "young_before_aux_kb": re.compile(r"Young before aux:\s+([0-9.]+)\s+KB"),
    "young_after_aux_kb": re.compile(r"Young after aux:\s+([0-9.]+)\s+KB"),
    "aux_total_kb": re.compile(r"Aux total:\s+([0-9.]+)\s+KB"),
    "giysb_staging_bytes": re.compile(r"GiYSB staging bytes:\s+([0-9]+)"),
    "giysb_tiny_table_bytes": re.compile(r"GiYSB tiny table bytes:\s*([0-9]+)"),
    "giysb_tiny_max": re.compile(r"GiYSB tiny max:\s+([0-9]+)"),
    "giysb_tiny_policy": re.compile(r"GiYSB tiny policy:\s+(.+)"),
    "nt_copy_width_bits": re.compile(r"NT copy width:\s+([0-9]+)\s+bits"),
}

INT_KEYS = {
    "workspace_after_rset_kb",
    "cache_size_kb_reported",
    "minor_gc_count",
    "write_barrier_calls",
    "write_barrier_duplicates",
    "total_allocations",
    "forward_operations",
    "giysb_staging_bytes",
    "giysb_tiny_table_bytes",
    "giysb_tiny_max",
    "nt_copy_width_bits",
}

STRING_KEYS = {"giysb_tiny_policy"}

GIYSB_PROFILE_PATTERNS = {
    "giysb_profile_enabled": re.compile(r"GiYSB profile:\s+([0-9]+)"),
    "giysb_tiny_reserved": re.compile(r"GiYSB tiny reserved:\s*([0-9]+) objs, ([0-9.]+) MB"),
    "giysb_tiny_overflow": re.compile(r"GiYSB tiny overflow:\s*([0-9]+) objs, ([0-9.]+) MB"),
    "giysb_large_reserved": re.compile(r"GiYSB large reserved:\s*([0-9]+) objs, ([0-9.]+) MB"),
    "giysb_staged_tiny": re.compile(r"GiYSB staged tiny:\s*([0-9]+) objs, ([0-9.]+) MB"),
    "giysb_direct_tiny": re.compile(r"GiYSB direct tiny:\s*([0-9]+) objs, ([0-9.]+) MB"),
    "giysb_max_tiny_table_entries": re.compile(r"GiYSB max tiny table:\s*([0-9]+) entries"),
    "giysb_staging_flushes": re.compile(r"GiYSB staging flushes:\s*([0-9]+) \(full ([0-9]+), tail ([0-9]+)\)"),
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
    row = {"time_real_sec": "", "time_user_sec": "", "time_sys_sec": ""}
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        return row
    for line in text.splitlines():
        parts = line.split()
        if len(parts) == 2 and parts[0] in ("real", "user", "sys"):
            row[f"time_{parts[0]}_sec"] = parse_float(parts[1])
    return row


def parse_out(path):
    try:
        text = path.read_text(errors="replace")
    except FileNotFoundError:
        text = ""
    row = {}
    for key, pattern in PATTERNS.items():
        match = pattern.search(text)
        value = match.group(1).strip() if match else ""
        if key in STRING_KEYS:
            row[key] = value
        elif key in INT_KEYS:
            row[key] = parse_int(value)
        else:
            row[key] = parse_float(value)

    match = GIYSB_PROFILE_PATTERNS["giysb_profile_enabled"].search(text)
    row["giysb_profile_enabled"] = parse_int(match.group(1)) if match else ""

    for prefix in (
        "giysb_tiny_reserved",
        "giysb_tiny_overflow",
        "giysb_large_reserved",
        "giysb_staged_tiny",
        "giysb_direct_tiny",
    ):
        match = GIYSB_PROFILE_PATTERNS[prefix].search(text)
        row[f"{prefix}_objects"] = parse_int(match.group(1)) if match else ""
        row[f"{prefix}_mb"] = parse_float(match.group(2)) if match else ""

    match = GIYSB_PROFILE_PATTERNS["giysb_max_tiny_table_entries"].search(text)
    row["giysb_max_tiny_table_entries"] = parse_int(match.group(1)) if match else ""

    match = GIYSB_PROFILE_PATTERNS["giysb_staging_flushes"].search(text)
    row["giysb_staging_flushes"] = parse_int(match.group(1)) if match else ""
    row["giysb_staging_full_flushes"] = parse_int(match.group(2)) if match else ""
    row["giysb_staging_tail_flushes"] = parse_int(match.group(3)) if match else ""

    staged_mb = row.get("giysb_staged_tiny_mb", "")
    flushes = row.get("giysb_staging_flushes", "")
    row["giysb_avg_staging_flush_kb"] = (
        staged_mb * 1024.0 / flushes
        if staged_mb != "" and flushes not in ("", 0)
        else ""
    )
    if row.get("business_logic_sec", "") == "":
        total = row.get("total_execution_sec", "")
        gc = row.get("gc_overhead_sec", "")
        row["business_logic_sec"] = total - gc if total != "" and gc != "" else ""
    return row


def discover_configs(out_base, benchmarks):
    configs = []
    for child in sorted(out_base.iterdir()):
        if not child.is_dir() or child.name == "smoke":
            continue
        if any((child / f"{bench}.out").exists() for bench in benchmarks):
            configs.append(child.name)
    return configs


def fmt(value, digits=3):
    if value == "" or value is None:
        return ""
    if isinstance(value, int):
        return str(value)
    return f"{value:.{digits}f}"


def pct(numerator, denominator):
    if numerator == "" or denominator in ("", 0):
        return ""
    return numerator / denominator * 100.0


def sum_key(rows, key):
    vals = [row[key] for row in rows if row.get(key, "") != ""]
    return sum(vals) if vals else ""


def first_value(rows, key):
    for row in rows:
        if row.get(key, "") != "":
            return row[key]
    return ""


def main():
    if len(sys.argv) != 2:
        print("usage: parse_giy_slot_cached_store_benchmarks.py OUT_BASE", file=sys.stderr)
        return 2

    out_base = Path(sys.argv[1]).resolve()
    info = parse_run_info(out_base / "run.info")
    benchmarks = info.get("benchmarks", "").split() or DEFAULT_BENCHMARKS
    configs = info.get("configs", "").split() or discover_configs(out_base, benchmarks)

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
        "giysb_staging_bytes",
        "giysb_tiny_table_bytes",
        "giysb_tiny_max",
        "giysb_tiny_policy",
        "nt_copy_width_bits",
        "giysb_profile_enabled",
        "total_execution_sec",
        "business_logic_sec",
        "gc_overhead_sec",
        "gc_core_sec",
        "scan_roots_sec",
        "scan_rs_sec",
        "scavenge_sec",
        "total_cpu_sec",
        "business_cpu_sec",
        "gc_cpu_sec",
        "minor_gc_count",
        "avg_gc_pause_ms",
        "write_barrier_calls",
        "write_barrier_duplicates",
        "total_allocations",
        "total_alloc_bytes_mb",
        "forward_operations",
        "giysb_tiny_reserved_objects",
        "giysb_tiny_reserved_mb",
        "giysb_tiny_overflow_objects",
        "giysb_tiny_overflow_mb",
        "giysb_large_reserved_objects",
        "giysb_large_reserved_mb",
        "giysb_staged_tiny_objects",
        "giysb_staged_tiny_mb",
        "giysb_direct_tiny_objects",
        "giysb_direct_tiny_mb",
        "giysb_max_tiny_table_entries",
        "giysb_staging_flushes",
        "giysb_staging_full_flushes",
        "giysb_staging_tail_flushes",
        "giysb_avg_staging_flush_kb",
        "time_real_sec",
        "time_user_sec",
        "time_sys_sec",
    ]

    csv_path = out_base / "summary.csv"
    with csv_path.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    completed_by_config = {
        config: [row for row in rows if row["config"] == config and row["status"] == "0"]
        for config in configs
    }

    metrics = [
        ("Total sec", "total_execution_sec"),
        ("Business sec", "business_logic_sec"),
        ("GC full sec", "gc_overhead_sec"),
        ("GC core sec", "gc_core_sec"),
        ("scan_roots sec", "scan_roots_sec"),
        ("scan_RS sec", "scan_rs_sec"),
        ("scavenge sec", "scavenge_sec"),
        ("Minor GC count", "minor_gc_count"),
        ("Forward ops", "forward_operations"),
        ("Write barriers", "write_barrier_calls"),
    ]

    md_path = out_base / "compare_slot_cached_store.md"
    baseline = configs[0] if configs else ""
    with md_path.open("w") as f:
        f.write("# GiY Slot Cached-Store Benchmark Summary\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write("Run settings:\n\n")
        f.write(f"- `GIY_OLD_SLOT_NT_STORE={info.get('giy_old_slot_nt_store', '')}`\n")
        f.write(f"- `GIY_NT_COPY_BITS={info.get('giy_nt_copy_bits', '')}`\n")
        f.write(f"- `CACHE_SIZE_KB={info.get('cache_size_kb', '')}`\n")
        f.write(f"- `GIY_GC_STACK_BYTES={info.get('giy_gc_stack_bytes', '')}`\n")
        f.write(f"- `GIYSB_PROFILE={info.get('giysb_profile', '')}`\n")
        f.write(f"- `GIY_LOCAL_PADDING_BYTES={info.get('giy_local_padding_bytes', '')}`\n\n")

        f.write("## Aggregate\n\n")
        f.write("| Metric | " + " | ".join(configs) + " |\n")
        f.write("|---" + "|---:" * len(configs) + "|\n")
        for label, key in metrics:
            values = [fmt(sum_key(completed_by_config[config], key)) for config in configs]
            f.write(f"| {label} | " + " | ".join(values) + " |\n")

        f.write("\n## Relative To First Config\n\n")
        f.write(f"Baseline: `{baseline}`\n\n")
        f.write("| Config | Total delta | Total delta % | GC full delta | GC full delta % | scan_RS delta | scan_RS delta % |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|\n")
        base_rows = completed_by_config.get(baseline, [])
        base_total = sum_key(base_rows, "total_execution_sec")
        base_gc = sum_key(base_rows, "gc_overhead_sec")
        base_rs = sum_key(base_rows, "scan_rs_sec")
        for config in configs:
            rows_for_config = completed_by_config[config]
            total = sum_key(rows_for_config, "total_execution_sec")
            gc = sum_key(rows_for_config, "gc_overhead_sec")
            rs = sum_key(rows_for_config, "scan_rs_sec")
            total_delta = total - base_total if total != "" and base_total != "" else ""
            gc_delta = gc - base_gc if gc != "" and base_gc != "" else ""
            rs_delta = rs - base_rs if rs != "" and base_rs != "" else ""
            f.write(
                f"| {config} | {fmt(total_delta)} | {fmt(pct(total_delta, base_total))}% | "
                f"{fmt(gc_delta)} | {fmt(pct(gc_delta, base_gc))}% | "
                f"{fmt(rs_delta)} | {fmt(pct(rs_delta, base_rs))}% |\n"
            )

        f.write("\n## Per-Benchmark Total Seconds\n\n")
        f.write("| Benchmark | " + " | ".join(configs) + " |\n")
        f.write("|---" + "|---:" * len(configs) + "|\n")
        by_key = {(row["config"], row["benchmark"]): row for row in rows}
        for bench in benchmarks:
            values = [fmt(by_key.get((config, bench), {}).get("total_execution_sec", "")) for config in configs]
            f.write(f"| {bench} | " + " | ".join(values) + " |\n")

        f.write("\n## Per-Benchmark GC Full Seconds\n\n")
        f.write("| Benchmark | " + " | ".join(configs) + " |\n")
        f.write("|---" + "|---:" * len(configs) + "|\n")
        for bench in benchmarks:
            values = [fmt(by_key.get((config, bench), {}).get("gc_overhead_sec", "")) for config in configs]
            f.write(f"| {bench} | " + " | ".join(values) + " |\n")

        if any(first_value(completed_by_config[config], "giysb_profile_enabled") != "" for config in configs):
            f.write("\n## GiYSB Profile Aggregate\n\n")
            f.write("| Metric | " + " | ".join(configs) + " |\n")
            f.write("|---" + "|---:" * len(configs) + "|\n")
            profile_sum_metrics = [
                ("Tiny reserved objs", "giysb_tiny_reserved_objects"),
                ("Tiny reserved MB", "giysb_tiny_reserved_mb"),
                ("Tiny overflow objs", "giysb_tiny_overflow_objects"),
                ("Tiny overflow MB", "giysb_tiny_overflow_mb"),
                ("Large reserved objs", "giysb_large_reserved_objects"),
                ("Large reserved MB", "giysb_large_reserved_mb"),
                ("Staged tiny objs", "giysb_staged_tiny_objects"),
                ("Staged tiny MB", "giysb_staged_tiny_mb"),
                ("Direct tiny objs", "giysb_direct_tiny_objects"),
                ("Direct tiny MB", "giysb_direct_tiny_mb"),
                ("Staging flushes", "giysb_staging_flushes"),
                ("Full flushes", "giysb_staging_full_flushes"),
                ("Tail flushes", "giysb_staging_tail_flushes"),
            ]
            for label, key in profile_sum_metrics:
                values = [fmt(sum_key(completed_by_config[config], key)) for config in configs]
                f.write(f"| {label} | " + " | ".join(values) + " |\n")
            values = []
            for config in configs:
                vals = [
                    row["giysb_max_tiny_table_entries"]
                    for row in completed_by_config[config]
                    if row.get("giysb_max_tiny_table_entries", "") != ""
                ]
                values.append(fmt(max(vals) if vals else ""))
            f.write("| Max tiny table entries | " + " | ".join(values) + " |\n")
            values = []
            for config in configs:
                staged_mb = sum_key(completed_by_config[config], "giysb_staged_tiny_mb")
                flushes = sum_key(completed_by_config[config], "giysb_staging_flushes")
                avg_kb = staged_mb * 1024.0 / flushes if staged_mb != "" and flushes not in ("", 0) else ""
                values.append(fmt(avg_kb))
            f.write("| Avg flush KB | " + " | ".join(values) + " |\n")

            f.write("\n## GiYSB Per-Benchmark Profile\n\n")
            f.write("| Benchmark | Config | staged MB | flushes | avg flush KB | max table | tiny reserved MB | large reserved MB |\n")
            f.write("|---|---|---:|---:|---:|---:|---:|---:|\n")
            for bench in benchmarks:
                for config in configs:
                    row = by_key.get((config, bench), {})
                    f.write(
                        f"| {bench} | {config} | "
                        f"{fmt(row.get('giysb_staged_tiny_mb', ''))} | "
                        f"{fmt(row.get('giysb_staging_flushes', ''))} | "
                        f"{fmt(row.get('giysb_avg_staging_flush_kb', ''))} | "
                        f"{fmt(row.get('giysb_max_tiny_table_entries', ''))} | "
                        f"{fmt(row.get('giysb_tiny_reserved_mb', ''))} | "
                        f"{fmt(row.get('giysb_large_reserved_mb', ''))} |\n"
                    )

        f.write("\n## Workspace\n\n")
        f.write("| Config | Young before aux KB | Young after aux KB | Aux total KB | GiYSB staging bytes | GiYSB tiny table bytes | Tiny max |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|\n")
        for config in configs:
            rows_for_config = completed_by_config[config]
            f.write(
                f"| {config} | {fmt(first_value(rows_for_config, 'young_before_aux_kb'))} | "
                f"{fmt(first_value(rows_for_config, 'young_after_aux_kb'))} | "
                f"{fmt(first_value(rows_for_config, 'aux_total_kb'))} | "
                f"{fmt(first_value(rows_for_config, 'giysb_staging_bytes'))} | "
                f"{fmt(first_value(rows_for_config, 'giysb_tiny_table_bytes'))} | "
                f"{fmt(first_value(rows_for_config, 'giysb_tiny_max'))} |\n"
            )

    print(csv_path)
    print(md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
