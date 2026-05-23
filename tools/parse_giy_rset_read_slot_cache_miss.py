#!/usr/bin/env python3
import csv
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


def parse_status(path):
    try:
        return path.read_text().strip()
    except FileNotFoundError:
        return ""


def parse_perf(path):
    events = {}
    try:
        lines = path.read_text(errors="replace").splitlines()
    except FileNotFoundError:
        return events

    for line in lines:
        if not line or line.startswith("#"):
            continue
        parts = line.split(",")
        if len(parts) < 3:
            continue
        raw_value = parts[0].strip()
        event = parts[2].strip()
        if raw_value == "<not counted>":
            events[event] = ""
            continue
        try:
            events[event] = float(raw_value)
        except ValueError:
            events[event] = ""
    return events


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
    return f"{value:.{digits}f}"


def main():
    if len(sys.argv) != 2:
        print("usage: parse_giy_rset_read_slot_cache_miss.py OUT_BASE", file=sys.stderr)
        return 2

    out_base = Path(sys.argv[1]).resolve()
    info = parse_run_info(out_base / "run.info")
    configs = info.get("configs", "").split() or DEFAULT_CONFIGS
    benchmarks = info.get("benchmarks", "").split() or DEFAULT_BENCHMARKS

    rows = []
    for config in configs:
        config_dir = out_base / config
        for bench in benchmarks:
            events = parse_perf(config_dir / f"{bench}.perf.csv")
            cycles = events.get("cycles", "")
            instructions = events.get("instructions", "")
            refs = events.get("cache-references", "")
            misses = events.get("cache-misses", "")
            row = {
                "config": config,
                "benchmark": bench,
                "status": parse_status(config_dir / f"{bench}.status"),
                "cycles": cycles,
                "instructions": instructions,
                "cache_references": refs,
                "cache_misses": misses,
                "cache_miss_rate_pct": safe_rate(misses, refs),
                "cache_mpki": safe_mpki(misses, instructions),
            }
            rows.append(row)

    summary_csv = out_base / "summary.csv"
    fieldnames = [
        "config",
        "benchmark",
        "status",
        "cycles",
        "instructions",
        "cache_references",
        "cache_misses",
        "cache_miss_rate_pct",
        "cache_mpki",
    ]
    with summary_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    by_bench = {}
    for row in rows:
        by_bench.setdefault(row["benchmark"], {})[row["config"]] = row

    diff_rows = []
    for bench in benchmarks:
        default = by_bench.get(bench, {}).get("giy_default")
        slot = by_bench.get(bench, {}).get("giy_rset_read_slot")
        if not default or not slot:
            continue
        d_rate = default["cache_miss_rate_pct"]
        s_rate = slot["cache_miss_rate_pct"]
        d_mpki = default["cache_mpki"]
        s_mpki = slot["cache_mpki"]
        diff_rows.append({
            "benchmark": bench,
            "default_cache_miss_rate_pct": d_rate,
            "read_slot_cache_miss_rate_pct": s_rate,
            "miss_rate_delta_pct_points": "" if d_rate == "" or s_rate == "" else s_rate - d_rate,
            "miss_rate_relative_change_pct": "" if d_rate == "" or s_rate == "" or d_rate == 0 else (s_rate - d_rate) / d_rate * 100.0,
            "default_cache_mpki": d_mpki,
            "read_slot_cache_mpki": s_mpki,
            "mpki_delta": "" if d_mpki == "" or s_mpki == "" else s_mpki - d_mpki,
            "mpki_relative_change_pct": "" if d_mpki == "" or s_mpki == "" or d_mpki == 0 else (s_mpki - d_mpki) / d_mpki * 100.0,
        })

    diff_csv = out_base / "cache_miss_strategy_diff.csv"
    diff_fields = [
        "benchmark",
        "default_cache_miss_rate_pct",
        "read_slot_cache_miss_rate_pct",
        "miss_rate_delta_pct_points",
        "miss_rate_relative_change_pct",
        "default_cache_mpki",
        "read_slot_cache_mpki",
        "mpki_delta",
        "mpki_relative_change_pct",
    ]
    with diff_csv.open("w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=diff_fields)
        writer.writeheader()
        writer.writerows(diff_rows)

    md = out_base / "summary.md"
    with md.open("w") as f:
        f.write("# GiY RSet Cache Miss Summary\n\n")
        f.write(f"Output directory: `{out_base}`\n\n")
        f.write("| Benchmark | Default miss rate % | Read-slot miss rate % | Delta pp | Default MPKI | Read-slot MPKI | MPKI delta |\n")
        f.write("|---|---:|---:|---:|---:|---:|---:|\n")
        for row in diff_rows:
            f.write(
                f"| {row['benchmark']} | "
                f"{fmt(row['default_cache_miss_rate_pct'])} | "
                f"{fmt(row['read_slot_cache_miss_rate_pct'])} | "
                f"{fmt(row['miss_rate_delta_pct_points'])} | "
                f"{fmt(row['default_cache_mpki'])} | "
                f"{fmt(row['read_slot_cache_mpki'])} | "
                f"{fmt(row['mpki_delta'])} |\n"
            )

    print(summary_csv)
    print(diff_csv)
    print(md)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
