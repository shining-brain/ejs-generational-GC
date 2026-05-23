#!/usr/bin/env python3
import csv
import re
import sys
from pathlib import Path


DEFAULT_CONFIGS = ["giy_default", "giy_rset_read_slot"]
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
    "young_before_aux_kb": re.compile(r"Young before aux:\s+([0-9.]+)\s+KB"),
    "young_after_aux_kb": re.compile(r"Young after aux:\s+([0-9.]+)\s+KB"),
    "aux_total_kb": re.compile(r"Aux total:\s+([0-9.]+)\s+KB"),
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


def parse_status(path):
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
        value = match.group(1) if match else ""
        if key in ("workspace_after_rset_kb", "cache_size_kb_reported", "minor_gc_count"):
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
        print("usage: parse_giy_rset_read_slot_benchmarks.py OUT_BASE", file=sys.stderr)
        return 2

    out_base = Path(sys.argv[1]).resolve()
    info = parse_run_info(out_base / "run.info")
    configs = info.get("configs", "").split() or DEFAULT_CONFIGS
    benchmarks = info.get("benchmarks", "").split() or DEFAULT_BENCHMARKS
    cache_size_kb = info.get("cache_size_kb", "")

    rows = []
    for config in configs:
        config_dir = out_base / config
        for bench in benchmarks:
            row = {
                "config": config,
                "cache_size_kb": cache_size_kb,
                "benchmark": bench,
                "status": parse_status(config_dir / f"{bench}.status"),
            }
            row.update(parse_out(config_dir / f"{bench}.out"))
            row.update(parse_time(config_dir / f"{bench}.time"))
            rows.append(row)

    fieldnames = [
        "config",
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
        "young_before_aux_kb",
        "young_after_aux_kb",
        "aux_total_kb",
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
        by_config[row["config"]][row["benchmark"]] = row

    md_path = out_base / "summary.md"
    with md_path.open("w") as f:
        f.write("# GiY RSet Read-Slot Benchmark Summary\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write(f"Cache size: `{cache_size_kb} KB`\n\n")
        f.write("| Benchmark | Default sec | Read-slot sec | Speedup vs default | Default GC sec | Read-slot GC sec |\n")
        f.write("|---|---:|---:|---:|---:|---:|\n")

        default_total = 0.0
        slot_total = 0.0
        default_gc = 0.0
        slot_gc = 0.0
        completed = 0
        for bench in benchmarks:
            default = by_config.get("giy_default", {}).get(bench, {})
            slot = by_config.get("giy_rset_read_slot", {}).get(bench, {})
            dt = default.get("total_execution_sec", "")
            st = slot.get("total_execution_sec", "")
            dgc = default.get("gc_overhead_sec", "")
            sgc = slot.get("gc_overhead_sec", "")
            speedup = ""
            if default.get("status") == "0" and slot.get("status") == "0" and dt != "" and st != "":
                speedup = (dt - st) / dt * 100.0
                default_total += dt
                slot_total += st
                if dgc != "":
                    default_gc += dgc
                if sgc != "":
                    slot_gc += sgc
                completed += 1
            f.write(
                f"| {bench} | {fmt(dt)} | {fmt(st)} | {fmt(speedup)}% | "
                f"{fmt(dgc)} | {fmt(sgc)} |\n"
            )

        f.write("\n## Aggregate\n\n")
        f.write("| Completed | Default total sec | Read-slot total sec | Speedup | Default GC sec | Read-slot GC sec | GC change |\n")
        f.write("|---:|---:|---:|---:|---:|---:|---:|\n")
        speedup = (default_total - slot_total) / default_total * 100.0 if default_total else ""
        gc_change = (default_gc - slot_gc) / default_gc * 100.0 if default_gc else ""
        f.write(
            f"| {completed} | {default_total:.3f} | {slot_total:.3f} | "
            f"{fmt(speedup)}% | {default_gc:.3f} | {slot_gc:.3f} | {fmt(gc_change)}% |\n"
        )

    print(csv_path)
    print(md_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
