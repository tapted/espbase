#!/usr/bin/env python3
"""
stack_json Codegen Analysis Toolchain

Compiles representative stack_json parser and builder snippets with size-optimized
flags (-Os -fverbose-asm), producing annotated assembly, object files, symbol size
breakdowns, and change-tracking diffs against baseline.

Usage:
    python run_codegen.py [--build-dir <dir>] [--compiler <path>] [--snippet <name>]
    python run_codegen.py --save-baseline
    python run_codegen.py --diff
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path


def find_tool(tool_name: str, compiler_path: str = None) -> str:
    """Find LLVM/tool executable, looking next to compiler first, then in PATH."""
    if compiler_path:
        compiler_dir = Path(compiler_path).parent
        candidate = compiler_dir / (tool_name + (".exe" if os.name == "nt" else ""))
        if candidate.is_file():
            return str(candidate)
        candidate = compiler_dir / ("llvm-" + tool_name + (".exe" if os.name == "nt" else ""))
        if candidate.is_file():
            return str(candidate)

    which = shutil.which(tool_name) or shutil.which("llvm-" + tool_name)
    return which or tool_name


def demangle_batch(names: list[str], cxxfilt_tool: str) -> dict[str, str]:
    """Demangle a list of symbol names in a single batch."""
    if not names:
        return {}
    try:
        proc = subprocess.run(
            [cxxfilt_tool],
            input="\n".join(names),
            capture_output=True,
            text=True,
            check=True
        )
        demangled = proc.stdout.splitlines()
        return dict(zip(names, demangled))
    except Exception:
        return {n: n for n in names}


def demangle_file(text: str, cxxfilt_tool: str) -> str:
    """Demangle an entire text file (e.g. assembly) using cxxfilt."""
    try:
        proc = subprocess.run(
            [cxxfilt_tool],
            input=text,
            capture_output=True,
            text=True,
            check=True
        )
        return proc.stdout
    except Exception:
        return text


def parse_sections(obj_file: Path, objdump_tool: str, cxxfilt_tool: str):
    """
    Parse section headers from objdump -h to get exact byte sizes of individual
    functions and data structures produced by -ffunction-sections -fdata-sections.
    """
    proc = subprocess.run([objdump_tool, "-h", str(obj_file)], capture_output=True, text=True)
    if proc.returncode != 0:
        return {"text": 0, "rodata": 0, "total": 0}, []

    sections = []
    mangled_names = []

    for line in proc.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) >= 3 and parts[0].isdigit() and len(parts[2]) == 8:
            sec_name = parts[1]
            try:
                size_bytes = int(parts[2], 16)
            except ValueError:
                continue

            if size_bytes == 0:
                continue

            # Skip debug and unwind information for size report
            if sec_name.startswith(".debug") or sec_name.startswith(".pdata") or sec_name.startswith(".xdata") or sec_name.startswith(".llvm_addrsig"):
                continue

            kind = "other"
            if sec_name.startswith(".text"):
                kind = "code"
            elif sec_name.startswith(".rdata") or sec_name.startswith(".rodata"):
                kind = "rodata"
            elif sec_name.startswith(".data") or sec_name.startswith(".bss"):
                kind = "data"

            # Check for mangled symbol after $
            mangled = None
            if "$" in sec_name:
                mangled = sec_name.split("$", 1)[1]
                mangled_names.append(mangled)

            sections.append({
                "raw_name": sec_name,
                "mangled": mangled,
                "size": size_bytes,
                "kind": kind
            })

    demangled_map = demangle_batch(mangled_names, cxxfilt_tool)

    text_bytes = 0
    rodata_bytes = 0
    symbols = []

    for sec in sections:
        if sec["kind"] == "code":
            text_bytes += sec["size"]
        elif sec["kind"] == "rodata":
            rodata_bytes += sec["size"]

        if sec["mangled"]:
            dname = demangled_map.get(sec["mangled"], sec["mangled"])
        else:
            dname = sec["raw_name"]

        symbols.append({
            "name": dname,
            "size": sec["size"],
            "kind": sec["kind"]
        })

    symbols.sort(key=lambda s: s["size"], reverse=True)
    sizes = {
        "text": text_bytes,
        "rodata": rodata_bytes,
        "total": text_bytes + rodata_bytes
    }
    return sizes, symbols


def compile_snippet(snippet: Path, output_dir: Path, compiler: str,
                    include_dirs: list[Path], cxxfilt: str, objdump: str):
    name = snippet.stem
    asm_raw = output_dir / f"{name}.s"
    asm_annotated = output_dir / f"{name}.annotated.s"
    obj_file = output_dir / f"{name}.o"
    sym_file = output_dir / f"{name}.symbols.txt"

    base_cmd = [
        compiler,
        "-std=gnu++26",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
    ]
    for inc in include_dirs:
        base_cmd.extend(["-I", str(inc)])

    # 1. Generate raw assembly with verbose compiler comments
    cmd_asm = base_cmd + ["-S", "-fverbose-asm", str(snippet), "-o", str(asm_raw)]
    res_asm = subprocess.run(cmd_asm, capture_output=True, text=True)
    if res_asm.returncode != 0:
        print(f"Error compiling {snippet.name} to assembly:\n{res_asm.stderr}", file=sys.stderr)
        return None

    # 2. Demangle assembly into annotated assembly file
    try:
        raw_text = asm_raw.read_text(encoding="utf-8", errors="replace")
        demangled_text = demangle_file(raw_text, cxxfilt)
        asm_annotated.write_text(demangled_text, encoding="utf-8")
    except Exception as e:
        print(f"Warning: Failed to demangle {asm_raw}: {e}", file=sys.stderr)

    # 3. Generate object file for exact section sizing
    cmd_obj = base_cmd + ["-c", str(snippet), "-o", str(obj_file)]
    res_obj = subprocess.run(cmd_obj, capture_output=True, text=True)
    if res_obj.returncode != 0:
        print(f"Error compiling {snippet.name} to object:\n{res_obj.stderr}", file=sys.stderr)
        return None

    # 4. Measure section sizes and symbol footprint
    sizes, symbols = parse_sections(obj_file, objdump, cxxfilt)

    # 5. Write symbols summary file
    with open(sym_file, "w", encoding="utf-8") as f:
        f.write(f"Symbol footprint for {snippet.name} (Code: {sizes['text']} B, ROData: {sizes['rodata']} B, Total: {sizes['total']} B)\n")
        f.write("=" * 90 + "\n")
        f.write(f"{'Size (Bytes)':<14} {'Kind':<10} Symbol\n")
        f.write("-" * 90 + "\n")
        for sym in symbols:
            f.write(f"{sym['size']:>6} B        {sym['kind']:<10} {sym['name']}\n")

    return {
        "name": name,
        "sizes": sizes,
        "symbols": symbols,
        "asm_annotated": asm_annotated,
        "asm_raw": asm_raw,
        "obj_file": obj_file
    }


def main():
    parser = argparse.ArgumentParser(description="Analyze stack_json codegen bloat.")
    parser.add_argument("--compiler", default=None, help="C++ compiler executable")
    parser.add_argument("--build-dir", default=None, help="Build directory output")
    parser.add_argument("--snippet", default=None, help="Specific snippet to compile (default: all)")
    parser.add_argument("--save-baseline", action="store_true", help="Save current measurements as baseline")
    parser.add_argument("--diff", action="store_true", help="Compare current results against saved baseline")
    args = parser.parse_args()

    script_dir = Path(__file__).resolve().parent
    snippets_dir = script_dir / "snippets"
    espbase_src = script_dir.parents[3] / "src"
    emulated_inc = script_dir.parents[3] / "emulated" / "esp-idf" / "include"

    if not args.build_dir:
        build_dir = script_dir.parents[2] / "build" / "codegen"
    else:
        build_dir = Path(args.build_dir)
        if not build_dir.name == "codegen":
            build_dir = build_dir / "codegen"

    build_dir.mkdir(parents=True, exist_ok=True)
    baseline_file = script_dir / "baseline_sizes.json"

    compiler = args.compiler
    if not compiler:
        compiler = shutil.which("clang++") or "C:/msys64/clang64/bin/clang++.exe"
    if not Path(compiler).is_file() and not shutil.which(compiler):
        print(f"Compiler not found: {compiler}", file=sys.stderr)
        sys.exit(1)

    cxxfilt = find_tool("llvm-cxxfilt", compiler)
    objdump = find_tool("llvm-objdump", compiler)

    include_dirs = [espbase_src, emulated_inc]

    snippet_files = sorted(snippets_dir.glob("*.cpp"))
    if args.snippet:
        snippet_files = [f for f in snippet_files if args.snippet in f.stem]
        if not snippet_files:
            print(f"No snippet matching '{args.snippet}' found in {snippets_dir}", file=sys.stderr)
            sys.exit(1)

    print(f"==========================================================================================")
    print(f" stack_json Codegen Analysis Toolchain")
    print(f" Compiler : {compiler}")
    print(f" Output   : {build_dir}")
    print(f" Snippets : {len(snippet_files)} test cases")
    print(f"==========================================================================================\n")

    results = []
    current_data = {}
    for snippet in snippet_files:
        res = compile_snippet(snippet, build_dir, compiler, include_dirs, cxxfilt, objdump)
        if res:
            results.append(res)
            current_data[res["name"]] = res["sizes"]

    # Handle baseline
    baseline_data = {}
    if baseline_file.is_file():
        try:
            baseline_data = json.loads(baseline_file.read_text(encoding="utf-8"))
        except Exception:
            pass

    if args.save_baseline:
        baseline_file.write_text(json.dumps(current_data, indent=2), encoding="utf-8")
        print(f" Saved current measurements as baseline to: {baseline_file}\n")
        baseline_data = current_data

    # Print summary table
    has_diff = bool(baseline_data) and (args.diff or args.save_baseline or True)
    if has_diff:
        header = f"{'Snippet Case':<30} {'Code (.text)':<13} {'ROData':<10} {'Total':<10} {'Diff vs Base':<14} {'Top Bloat Contributor'}"
    else:
        header = f"{'Snippet Case':<30} {'Code (.text)':<13} {'ROData':<10} {'Total':<10} {'Top Bloat Contributor'}"

    print(header)
    print("-" * len(header))

    report_lines = [
        "# stack_json Codegen Analysis Report",
        "",
        f"- **Compiler:** `{compiler}`",
        f"- **Optimization:** `-std=gnu++26 -Os -ffunction-sections -fdata-sections -fverbose-asm`",
        f"- **Snippets Analyzed:** {len(results)}",
        "",
        "| Snippet Case | .text (code) | .rodata (vtables) | Total Bytes | Diff vs Base | Top Bloat Contributor |",
        "|:---|---:|---:|---:|---:|:---|"
    ]

    total_text = 0
    total_rodata = 0
    total_all = 0
    total_base = 0

    for r in results:
        name = r["name"]
        sz = r["sizes"]
        top_sym = "-"
        if r["symbols"]:
            top_sym = r["symbols"][0]["name"]
        top_sym_short = (top_sym[:38] + "...") if len(top_sym) > 38 else top_sym

        total_text += sz["text"]
        total_rodata += sz["rodata"]
        total_all += sz["total"]

        diff_str = "-"
        if name in baseline_data:
            b_tot = baseline_data[name].get("total", sz["total"])
            total_base += b_tot
            delta = sz["total"] - b_tot
            if delta > 0:
                diff_str = f"+{delta} B"
            elif delta < 0:
                diff_str = f"{delta} B"
            else:
                diff_str = "0 B"

        if has_diff:
            print(f"{name:<30} {sz['text']:>8} B    {sz['rodata']:>6} B   {sz['total']:>6} B   {diff_str:>12}   {top_sym_short}")
        else:
            print(f"{name:<30} {sz['text']:>8} B    {sz['rodata']:>6} B   {sz['total']:>6} B   {top_sym_short}")

        report_lines.append(f"| [`{name}`](./{name}.annotated.s) | {sz['text']} B | {sz['rodata']} B | **{sz['total']} B** | {diff_str} | `{top_sym_short}` |")

    tot_diff_str = "-"
    if total_base > 0:
        tot_delta = total_all - total_base
        tot_diff_str = f"{'+' if tot_delta > 0 else ''}{tot_delta} B"

    print("-" * len(header))
    if has_diff:
        print(f"{'TOTAL CORPUS':<30} {total_text:>8} B    {total_rodata:>6} B   {total_all:>6} B   {tot_diff_str:>12}")
    else:
        print(f"{'TOTAL CORPUS':<30} {total_text:>8} B    {total_rodata:>6} B   {total_all:>6} B")
    report_lines.append(f"| **TOTAL CORPUS** | **{total_text} B** | **{total_rodata} B** | **{total_all} B** | **{tot_diff_str}** | |")

    # Detailed symbol breakdowns per snippet
    report_lines.extend(["", "---", "", "## Detailed Symbol Breakdown per Snippet", ""])
    for r in results:
        report_lines.append(f"### [`{r['name']}`](./{r['name']}.annotated.s) — Total: {r['sizes']['total']} B (Code: {r['sizes']['text']} B, ROData: {r['sizes']['rodata']} B)")
        report_lines.append(f"- Annotated Assembly: [`{r['name']}.annotated.s`](./{r['name']}.annotated.s)")
        report_lines.append(f"- Raw Assembly: [`{r['name']}.s`](./{r['name']}.s)")
        report_lines.append(f"- Symbol Dump: [`{r['name']}.symbols.txt`](./{r['name']}.symbols.txt)")
        report_lines.append("")
        report_lines.append("| Bytes | Section Type | Demangled Symbol |")
        report_lines.append("|---:|:---:|:---|")
        for sym in r["symbols"][:10]:
            report_lines.append(f"| {sym['size']} | `{sym['kind']}` | `{sym['name']}` |")
        report_lines.append("")

    report_path = build_dir / "codegen_report.md"
    report_path.write_text("\n".join(report_lines), encoding="utf-8")
    print(f"\nAnnotated assembly files and report generated in:\n  {build_dir}\n")


if __name__ == "__main__":
    main()
