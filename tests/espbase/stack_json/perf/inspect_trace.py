#!/usr/bin/env python3
"""
stack_json Offline samply Trace Inspector

Analyzes Firefox Profiler / samply trace archives (.json.gz or .json), parsing
thread execution samples, call trees, and symbol tables to display top functions
ranked by self-time (exclusive) and total running time (inclusive).

Usage:
    python inspect_trace.py [trace.json.gz] [--top 20] [--filter <str>]
"""

import argparse
import gzip
import json
import os
import sys
from pathlib import Path


def load_trace_data(trace_path: Path) -> dict:
    if not trace_path.is_file():
        print(f"Error: Trace file not found: {trace_path}", file=sys.stderr)
        sys.exit(1)

    try:
        if trace_path.suffix == ".gz":
            with gzip.open(trace_path, "rt", encoding="utf-8") as f:
                return json.load(f)
        else:
            with open(trace_path, "r", encoding="utf-8") as f:
                return json.load(f)
    except Exception as e:
        print(f"Error loading trace {trace_path}: {e}", file=sys.stderr)
        sys.exit(1)


def find_default_trace(script_dir: Path) -> Path | None:
    candidates = [
        Path("trace.json.gz"),
        script_dir.parents[3] / "trace.json.gz",  # deps/espbase/trace.json.gz
        script_dir.parents[2] / "build" / "perf" / "trace.json.gz",
        Path("trace.json"),
    ]
    for c in candidates:
        if c.is_file():
            return c
    return None


def analyze_thread(thread: dict, top_n: int, name_filter: str | None = None):
    name = thread.get("name", "unnamed_thread")
    tid = thread.get("tid", "")
    samples = thread.get("samples", {})
    func_table = thread.get("funcTable", {})
    string_table = thread.get("stringArray", [])
    stack_table = thread.get("stackTable", {})
    frame_table = thread.get("frameTable", {})

    sample_stacks = samples.get("stack", [])
    if not sample_stacks or not func_table:
        return

    func_names = [
        string_table[i] if i < len(string_table) else f"func_{i}"
        for i in func_table.get("name", [])
    ]
    frame_funcs = frame_table.get("func", [])
    stack_frames = stack_table.get("frame", [])
    stack_prefixes = stack_table.get("prefix", [])

    self_counts: dict[int, int] = {}
    total_counts: dict[int, int] = {}

    for s in sample_stacks:
        if s is None or s < 0 or s >= len(stack_frames):
            continue
        leaf_frame = stack_frames[s]
        if leaf_frame < len(frame_funcs):
            f_idx = frame_funcs[leaf_frame]
            self_counts[f_idx] = self_counts.get(f_idx, 0) + 1

        curr = s
        seen_funcs = set()
        while curr is not None and curr >= 0 and curr < len(stack_frames):
            f = stack_frames[curr]
            if f < len(frame_funcs):
                func_id = frame_funcs[f]
                if func_id not in seen_funcs:
                    seen_funcs.add(func_id)
                    total_counts[func_id] = total_counts.get(func_id, 0) + 1
            curr = stack_prefixes[curr] if curr < len(stack_prefixes) else None

    total_samples = len(sample_stacks)
    print("=" * 105)
    print(f" Thread: {name} (TID: {tid}) — Total Samples: {total_samples:,d}")
    print("=" * 105)

    # Filter functions if requested
    def matches_filter(fname: str) -> bool:
        if not name_filter:
            return True
        return name_filter.lower() in fname.lower()

    # Self-Time Table
    print(f"\n--- TOP SELF TIME (Time executing directly inside function body) ---")
    print(f"{'Self %':<9} {'Samples':>9}   Function / Symbol")
    print("-" * 105)
    sorted_self = sorted(self_counts.items(), key=lambda x: x[1], reverse=True)
    printed = 0
    for fid, count in sorted_self:
        fname = func_names[fid] if fid < len(func_names) else f"func_{fid}"
        if not matches_filter(fname):
            continue
        pct = (count / total_samples) * 100.0
        print(f"{pct:>6.1f} %   {count:>9,d}   {fname}")
        printed += 1
        if printed >= top_n:
            break

    # Running-Time Table
    print(f"\n--- TOP RUNNING TIME (Inclusive of all callees) ---")
    print(f"{'Total %':<9} {'Samples':>9}   Function / Symbol")
    print("-" * 105)
    sorted_total = sorted(total_counts.items(), key=lambda x: x[1], reverse=True)
    printed = 0
    for fid, count in sorted_total:
        fname = func_names[fid] if fid < len(func_names) else f"func_{fid}"
        if not matches_filter(fname):
            continue
        pct = (count / total_samples) * 100.0
        print(f"{pct:>6.1f} %   {count:>9,d}   {fname}")
        printed += 1
        if printed >= top_n:
            break
    print()


def main():
    script_dir = Path(__file__).resolve().parent

    parser = argparse.ArgumentParser(description="Analyze samply trace profile offline.")
    parser.add_argument("trace", nargs="?", default=None,
                        help="Path to trace file (.json.gz or .json)")
    parser.add_argument("--top", type=int, default=20,
                        help="Number of top functions to display (default: 20)")
    parser.add_argument("--filter", type=str, default=None,
                        help="Filter symbols by substring (case-insensitive)")
    parser.add_argument("--thread", type=str, default=None,
                        help="Filter by thread name")

    args = parser.parse_args()

    trace_path = None
    if args.trace:
        trace_path = Path(args.trace)
    else:
        trace_path = find_default_trace(script_dir)

    if not trace_path or not trace_path.is_file():
        print("Error: No trace file provided or found.", file=sys.stderr)
        print("Usage: python inspect_trace.py path/to/trace.json.gz", file=sys.stderr)
        sys.exit(1)

    print(f"Loading trace: {trace_path} ...")
    data = load_trace_data(trace_path)

    threads = data.get("threads", [])
    if not threads:
        print("No threads found in trace file.", file=sys.stderr)
        sys.exit(1)

    # Sort threads by sample count so the main worker thread is processed first
    threads.sort(key=lambda t: len(t.get("samples", {}).get("stack", [])), reverse=True)

    for thread in threads:
        tname = thread.get("name", "")
        if args.thread and args.thread.lower() not in tname.lower():
            continue
        if len(thread.get("samples", {}).get("stack", [])) == 0:
            continue
        analyze_thread(thread, top_n=args.top, name_filter=args.filter)


if __name__ == "__main__":
    main()

