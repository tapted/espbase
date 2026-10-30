#!/usr/bin/env python3
"""
stack_json In-Memory Performance Benchmark Runner

Executes the high-throughput stack_json in-memory benchmark executable across 5 representative
payload workloads (Primitives, Floats, Strings, Nested Paths, Mixed IoT), tracking build/parse
MB/s throughput and objects/second, with optional baseline saving and diffing.

Usage:
    python run_perf.py [--build-dir <dir>] [--mb 100] [--iterations 3]
    python run_perf.py --save-baseline
"""

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path


def format_num(n: float, decimals: int = 1) -> str:
    return f"{n:,.{decimals}f}"


def format_ops(n: float) -> str:
    if n >= 1_000_000:
        return f"{n / 1_000_000:.2f} M/s"
    elif n >= 1_000:
        return f"{n / 1_000:.1f} k/s"
    return f"{n:.0f} /s"


def format_diff(val: float, base: float) -> str:
    if base <= 0:
        return "  ---  "
    pct = ((val - base) / base) * 100.0
    sign = "+" if pct > 0 else ""
    return f"{sign}{pct:.1f}%"


def main():
    script_dir = Path(__file__).resolve().parent
    repo_tests_dir = script_dir.parent.parent.parent  # tests/

    parser = argparse.ArgumentParser(description="stack_json Performance Benchmark Runner")
    parser.add_argument("--build-dir", type=Path, default=repo_tests_dir / "build",
                        help="Path to CMake build directory containing stack_json_perf executable")
    parser.add_argument("--mb", type=int, default=100,
                        help="Target corpus RAM size per workload in megabytes (default: 100)")
    parser.add_argument("--iterations", type=int, default=3,
                        help="Number of iterations to average per workload (default: 3)")
    parser.add_argument("--filter", type=str, default="",
                        help="Filter to run only matching workload")
    parser.add_argument("--save-baseline", action="store_true",
                        help="Save current benchmark results as baseline in baseline_perf.json")
    parser.add_argument("--baseline", type=Path, default=script_dir / "baseline_perf.json",
                        help="Path to baseline JSON file")

    args = parser.parse_args()

    exe_name = "stack_json_perf.exe" if os.name == "nt" else "stack_json_perf"
    exe_path = args.build_dir / exe_name

    if not exe_path.is_file():
        # Try Debug/Release subdirectories
        for sub in ["Release", "Debug", "RelWithDebInfo"]:
            candidate = args.build_dir / sub / exe_name
            if candidate.is_file():
                exe_path = candidate
                break

    if not exe_path.is_file():
        print(f"Error: Could not find benchmark executable at: {exe_path}", file=sys.stderr)
        print("Please build it first with: ninja -C tests/build stack_json_perf", file=sys.stderr)
        sys.exit(1)

    cmd = [
        str(exe_path),
        "--mb", str(args.mb),
        "--iterations", str(args.iterations),
        "--json"
    ]
    if args.filter:
        cmd.extend(["--filter", args.filter])

    print("=" * 105)
    print(" stack_json In-Memory Performance Benchmark")
    print(f" Executable : {exe_path}")
    print(f" Target RAM : {args.mb} MB per workload ({args.iterations} iterations per test)")
    print("=" * 105)

    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"Error running benchmark:\n{proc.stderr}", file=sys.stderr)
        sys.exit(proc.returncode)

    try:
        data = json.loads(proc.stdout)
    except json.JSONDecodeError as e:
        print(f"Failed to parse JSON benchmark output: {e}\nRaw output:\n{proc.stdout}", file=sys.stderr)
        sys.exit(1)

    results = data.get("results", [])

    baseline = {}
    if args.baseline.is_file():
        try:
            with open(args.baseline, "r", encoding="utf-8") as f:
                baseline = json.load(f)
        except Exception as e:
            print(f"Warning: Could not read baseline file {args.baseline}: {e}", file=sys.stderr)

    if args.save_baseline:
        baseline_data = {
            "target_mb": args.mb,
            "iterations": args.iterations,
            "workloads": {r["name"]: r for r in results}
        }
        with open(args.baseline, "w", encoding="utf-8") as f:
            json.dump(baseline_data, f, indent=2)
        print(f"\n Saved current results as baseline to: {args.baseline}\n")

    base_workloads = baseline.get("workloads", {})

    # Print Table
    header = (
        f"{'Workload':<15} {'Size (MB)':>10} {'Objects':>11} "
        f"{'Build (MB/s)':>13} {'Build Diff':>11} "
        f"{'Parse (MB/s)':>13} {'Parse Diff':>11} "
        f"{'Parse Ops':>12} {'Status':>8}"
    )
    print(header)
    print("-" * 105)

    total_bytes = 0
    total_objects = 0
    total_build_ms = 0.0
    total_parse_ms = 0.0
    all_pass = True

    for r in results:
        name = r["name"]
        size_mb = r["actual_bytes"] / (1024.0 * 1024.0)
        num_objs = r["num_objects"]
        build_mbs = r["build_mbs"]
        parse_mbs = r["parse_mbs"]
        parse_ops = r["parse_ops_sec"]
        status = "PASS" if r.get("verified", False) else "FAIL"

        total_bytes += r["actual_bytes"]
        total_objects += num_objs
        total_build_ms += r["build_time_ms"]
        total_parse_ms += r["parse_time_ms"]
        if status != "PASS":
            all_pass = False

        b_entry = base_workloads.get(name, {})
        b_build_mbs = b_entry.get("build_mbs", 0.0)
        b_parse_mbs = b_entry.get("parse_mbs", 0.0)

        build_diff = format_diff(build_mbs, b_build_mbs) if b_build_mbs > 0 else "   ---   "
        parse_diff = format_diff(parse_mbs, b_parse_mbs) if b_parse_mbs > 0 else "   ---   "

        print(
            f"{name:<15} {size_mb:>8.1f} MB {num_objs:>11,d} "
            f"{build_mbs:>10.1f} MB/s {build_diff:>11} "
            f"{parse_mbs:>10.1f} MB/s {parse_diff:>11} "
            f"{format_ops(parse_ops):>12} {status:>8}"
        )

    print("-" * 105)
    overall_mb = total_bytes / (1024.0 * 1024.0)
    overall_build_mbs = overall_mb / (total_build_ms / 1000.0) if total_build_ms > 0 else 0.0
    overall_parse_mbs = overall_mb / (total_parse_ms / 1000.0) if total_parse_ms > 0 else 0.0
    overall_parse_ops = total_objects / (total_parse_ms / 1000.0) if total_parse_ms > 0 else 0.0

    print(
        f"{'TOTAL / AVG':<15} {overall_mb:>8.1f} MB {total_objects:>11,d} "
        f"{overall_build_mbs:>10.1f} MB/s {'':>11} "
        f"{overall_parse_mbs:>10.1f} MB/s {'':>11} "
        f"{format_ops(overall_parse_ops):>12} {'PASS' if all_pass else 'FAIL':>8}"
    )
    print("=" * 105)

    # Write Markdown Report to build directory
    perf_out_dir = args.build_dir / "perf"
    perf_out_dir.mkdir(parents=True, exist_ok=True)
    report_file = perf_out_dir / "perf_report.md"

    with open(report_file, "w", encoding="utf-8") as f:
        f.write("# stack_json Performance Benchmark Report\n\n")
        f.write(f"- **Target Corpus RAM:** {args.mb} MB per workload\n")
        f.write(f"- **Iterations Averaged:** {args.iterations}\n")
        f.write(f"- **Overall Throughput:** Build {overall_build_mbs:.1f} MB/s | Parse {overall_parse_mbs:.1f} MB/s ({format_ops(overall_parse_ops)})\n\n")
        f.write("| Workload | Size (MB) | Objects | Build Time | Build Throughput | Parse Time | Parse Throughput | Parse Ops/s | Status |\n")
        f.write("|:---|---:|---:|---:|---:|---:|---:|---:|:---:|\n")
        for r in results:
            mb = r["actual_bytes"] / (1024.0 * 1024.0)
            status = "PASS" if r.get("verified", False) else "FAIL"
            f.write(
                f"| `{r['name']}` | {mb:.1f} MB | {r['num_objects']:,d} | "
                f"{r['build_time_ms']:.1f} ms | **{r['build_mbs']:.1f} MB/s** | "
                f"{r['parse_time_ms']:.1f} ms | **{r['parse_mbs']:.1f} MB/s** | "
                f"{format_ops(r['parse_ops_sec'])} | {status} |\n"
            )
        f.write(
            f"| **TOTAL / AVG** | **{overall_mb:.1f} MB** | **{total_objects:,d}** | "
            f"**{total_build_ms:.1f} ms** | **{overall_build_mbs:.1f} MB/s** | "
            f"**{total_parse_ms:.1f} ms** | **{overall_parse_mbs:.1f} MB/s** | "
            f"**{format_ops(overall_parse_ops)}** | **{'PASS' if all_pass else 'FAIL'}** |\n"
        )

    print(f"\nDetailed report written to: {report_file}\n")


if __name__ == "__main__":
    main()

