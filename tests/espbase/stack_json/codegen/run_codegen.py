#!/usr/bin/env python3
"""
stack_json Codegen Analysis Toolchain

Compiles representative stack_json parser and builder snippets with size-optimized
flags (-Os -fverbose-asm), producing annotated assembly, object files, symbol size
breakdowns, and complete dead-code-eliminated (DCE) linked binary footprints.

Tracks both:
1. Isolated template call-site bloat (snippet .o section sizes).
2. Complete optimized and tree-shaken linked binary footprint (-Wl,--gc-sections -Wl,-s)
   incorporating tree-shakable library implementations (buffer.cpp, document.cpp,
   parser.cpp, pretty_buffer.cpp, printer.cpp).

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


def parse_pe_sections(exe_file: Path, objdump_tool: str) -> dict:
    """Parse PE section sizes (.text and .rdata) of a linked executable."""
    proc = subprocess.run([objdump_tool, "-h", str(exe_file)], capture_output=True, text=True)
    if proc.returncode != 0:
        return {"text": 0, "rodata": 0, "total": 0, "filesize": 0}

    text_sz = 0
    rodata_sz = 0
    for line in proc.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) >= 3 and parts[0].isdigit():
            sec_name = parts[1]
            try:
                sz = int(parts[2], 16)
            except ValueError:
                continue
            if sec_name == ".text":
                text_sz += sz
            elif sec_name in (".rdata", ".rodata"):
                rodata_sz += sz

    filesize = exe_file.stat().st_size if exe_file.is_file() else 0
    return {
        "text": text_sz,
        "rodata": rodata_sz,
        "total": text_sz + rodata_sz,
        "filesize": filesize
    }


def compile_library_objects(lib_dir: Path, output_dir: Path, compiler: str, include_dirs: list[Path]) -> list[Path]:
    """Precompile stack_json core translation units with -Os -ffunction-sections -fdata-sections."""
    obj_dir = output_dir / "lib"
    obj_dir.mkdir(parents=True, exist_ok=True)

    sources = [
        lib_dir / "buffer.cpp",
        lib_dir / "document.cpp",
        lib_dir / "parser.cpp",
        lib_dir / "pretty_buffer.cpp",
        lib_dir / "printer.cpp",
    ]

    base_cmd = [
        compiler,
        "-std=gnu++26",
        "-Os",
        "-ffunction-sections",
        "-fno-rtti",
        "-fdata-sections",
    ]
    for inc in include_dirs:
        base_cmd.extend(["-I", str(inc)])

    compiled_objs = []
    for src in sources:
        if not src.is_file():
            continue
        obj = obj_dir / f"{src.stem}.o"
        # Compile if missing or older than source
        if not obj.is_file() or obj.stat().st_mtime < src.stat().st_mtime:
            cmd = base_cmd + ["-c", str(src), "-o", str(obj)]
            res = subprocess.run(cmd, capture_output=True, text=True)
            if res.returncode != 0:
                print(f"Error compiling library {src.name}:\n{res.stderr}", file=sys.stderr)
                sys.exit(1)
        compiled_objs.append(obj)

    return compiled_objs


def build_empty_executable(output_dir: Path, compiler: str, objdump: str) -> dict:
    """Build an empty main executable to measure baseline CRT code and rodata overhead."""
    empty_cpp = output_dir / "empty.cpp"
    empty_cpp.write_text("int main() { return 0; }\n", encoding="utf-8")
    empty_exe = output_dir / "empty.exe"

    cmd = [
        compiler,
        "-std=gnu++26",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-rtti",
        str(empty_cpp),
        "-Wl,--gc-sections",
        "-Wl,-s",
        "-o",
        str(empty_exe)
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Error building empty baseline executable:\n{res.stderr}", file=sys.stderr)
        return {"text": 0, "rodata": 0, "total": 0, "filesize": 0}

    return parse_pe_sections(empty_exe, objdump)


def find_entry_symbol(obj_file: Path, nm_tool: str) -> str | None:
    """Find the mangled snippet function symbol using nm."""
    proc = subprocess.run([nm_tool, "--defined-only", str(obj_file)], capture_output=True, text=True)
    if proc.returncode != 0:
        return None

    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[1] in ("T", "t") and "snippet_" in parts[2]:
            return parts[2]
    return None


def link_snippet_dce(name: str, entry_sym: str, obj_file: Path, lib_objs: list[Path],
                     empty_sizes: dict, output_dir: Path, compiler: str, objdump: str) -> dict:
    """
    Link a minimal executable calling the snippet with --gc-sections to measure
    net linked footprint (snippet + necessary library functions).
    """
    stub_dir = output_dir / "stubs"
    stub_dir.mkdir(parents=True, exist_ok=True)
    stub_file = stub_dir / f"stub_{name}.cpp"
    stub_file.write_text(
        f'extern "C" void {entry_sym}();\n'
        f'int main(int argc, char** argv) {{\n'
        f'    if (argc > 99999) {{\n'
        f'        {entry_sym}();\n'
        f'    }}\n'
        f'    return 0;\n'
        f'}}\n',
        encoding="utf-8"
    )

    linked_exe = output_dir / f"{name}.linked.exe"
    cmd = [
        compiler,
        "-std=gnu++26",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-rtti",
        str(stub_file),
        str(obj_file),
        *[str(o) for o in lib_objs],
        "-Wl,--gc-sections",
        "-Wl,-s",
        "-o",
        str(linked_exe)
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Warning: Failed to link DCE binary for {name}:\n{res.stderr}", file=sys.stderr)
        return {"net_text": 0, "net_rodata": 0, "net_total": 0, "raw": {}, "exe": None}

    raw = parse_pe_sections(linked_exe, objdump)
    net_text = max(0, raw["text"] - empty_sizes["text"])
    net_rodata = max(0, raw["rodata"] - empty_sizes["rodata"])
    return {
        "net_text": net_text,
        "net_rodata": net_rodata,
        "net_total": net_text + net_rodata,
        "raw": raw,
        "exe": linked_exe
    }


def link_corpus_dce(entries: list[tuple[str, Path]], lib_objs: list[Path],
                    empty_sizes: dict, output_dir: Path, compiler: str, objdump: str) -> dict:
    """Link all snippets into a single unified binary to measure complete corpus footprint."""
    stub_file = output_dir / "stub_corpus.cpp"
    decls = "\n".join([f'extern "C" void {sym}();' for sym, _ in entries if sym])
    calls = "\n".join([f'        {sym}();' for sym, _ in entries if sym])
    stub_file.write_text(
        f'{decls}\n'
        f'int main(int argc, char** argv) {{\n'
        f'    if (argc > 99999) {{\n'
        f'{calls}\n'
        f'    }}\n'
        f'    return 0;\n'
        f'}}\n',
        encoding="utf-8"
    )

    corpus_exe = output_dir / "stack_json_corpus_stripped.exe"
    obj_paths = [str(obj) for _, obj in entries]
    cmd = [
        compiler,
        "-std=gnu++26",
        "-Os",
        "-ffunction-sections",
        "-fdata-sections",
        "-fno-rtti",
        str(stub_file),
        *obj_paths,
        *[str(o) for o in lib_objs],
        "-Wl,--gc-sections",
        "-Wl,-s",
        "-o",
        str(corpus_exe)
    ]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0:
        print(f"Warning: Failed to link corpus executable:\n{res.stderr}", file=sys.stderr)
        return {"net_text": 0, "net_rodata": 0, "net_total": 0, "raw": {}, "exe": None}

    raw = parse_pe_sections(corpus_exe, objdump)
    net_text = max(0, raw["text"] - empty_sizes["text"])
    net_rodata = max(0, raw["rodata"] - empty_sizes["rodata"])
    return {
        "net_text": net_text,
        "net_rodata": net_rodata,
        "net_total": net_text + net_rodata,
        "raw": raw,
        "exe": corpus_exe
    }


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
        "-fno-rtti",
        "-fdata-sections",
    ]
    for inc in include_dirs:
        base_cmd.extend(["-I", str(inc)])

    # 1. Generate raw assembly with verbose compiler comments
    cmd_asm = base_cmd + ["-g", "-S", "-fverbose-asm", str(snippet), "-o", str(asm_raw)]
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
    stack_json_src = espbase_src / "espbase" / "stack_json"
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
    nm = find_tool("llvm-nm", compiler)

    include_dirs = [espbase_src, emulated_inc]

    snippet_files = sorted(snippets_dir.glob("*.cpp"))
    if args.snippet:
        snippet_files = [f for f in snippet_files if args.snippet in f.stem]
        if not snippet_files:
            print(f"No snippet matching '{args.snippet}' found in {snippets_dir}", file=sys.stderr)
            sys.exit(1)

    print(f"================================================================================================================")
    print(f" stack_json Codegen & Binary Bloat Analysis Toolchain")
    print(f" Compiler : {compiler}")
    print(f" Output   : {build_dir}")
    print(f" Snippets : {len(snippet_files)} test cases")
    print(f"================================================================================================================\n")

    # 1. Compile library objects
    lib_objs = compile_library_objects(stack_json_src, build_dir, compiler, include_dirs)

    # 2. Build empty baseline executable for CRT subtraction
    empty_sizes = build_empty_executable(build_dir, compiler, objdump)

    # 3. Compile snippets and link individual DCE binaries
    results = []
    corpus_entries = []

    for snippet in snippet_files:
        res = compile_snippet(snippet, build_dir, compiler, include_dirs, cxxfilt, objdump)
        if not res:
            continue

        entry_sym = find_entry_symbol(res["obj_file"], nm)
        linked = link_snippet_dce(
            res["name"], entry_sym, res["obj_file"], lib_objs,
            empty_sizes, build_dir, compiler, objdump
        )
        res["entry_sym"] = entry_sym
        res["linked"] = linked
        results.append(res)
        if entry_sym:
            corpus_entries.append((entry_sym, res["obj_file"]))

    # 4. Link complete multi-snippet corpus binary
    corpus_linked = link_corpus_dce(corpus_entries, lib_objs, empty_sizes, build_dir, compiler, objdump)

    # Prepare structured current data
    current_data = {
        "corpus": {
            "template_total": sum(r["sizes"]["total"] for r in results),
            "linked_net_text": corpus_linked["net_text"],
            "linked_net_rodata": corpus_linked["net_rodata"],
            "linked_net_total": corpus_linked["net_total"],
            "filesize": corpus_linked["raw"].get("filesize", 0)
        },
        "snippets": {}
    }
    for r in results:
        current_data["snippets"][r["name"]] = {
            "template": r["sizes"],
            "linked": {
                "net_text": r["linked"]["net_text"],
                "net_rodata": r["linked"]["net_rodata"],
                "net_total": r["linked"]["net_total"]
            }
        }

    # Handle baseline
    baseline_data = {}
    if baseline_file.is_file():
        try:
            raw_base = json.loads(baseline_file.read_text(encoding="utf-8"))
            if "snippets" in raw_base:
                baseline_data = raw_base
            else:
                # Migrate legacy baseline structure
                baseline_data = {"corpus": {}, "snippets": {}}
                for k, v in raw_base.items():
                    baseline_data["snippets"][k] = {"template": v, "linked": {}}
        except Exception:
            baseline_data = {}

    if args.save_baseline:
        baseline_file.write_text(json.dumps(current_data, indent=2), encoding="utf-8")
        print(f" Saved current measurements as baseline to: {baseline_file}\n")
        baseline_data = current_data

    # Print summary table
    has_diff = bool(baseline_data.get("snippets"))
    header = (
        f"{'Snippet Case':<26} "
        f"{'Tmpl .o':>9} "
        f"{'Net Linked':>11} "
        f"{'Net .text':>10} "
        f"{'Net .rdata':>11} "
        f"{'Diff Tmpl / Link':>18}  "
        f"{'Top Bloat Contributor'}"
    )

    print(header)
    print("-" * len(header))

    report_lines = [
        "# stack_json Codegen & Binary Bloat Analysis Report",
        "",
        f"- **Compiler:** `{compiler}`",
        f"- **Optimization Flags:** `-std=gnu++26 -Os -ffunction-sections -fdata-sections -fno-rtti -Wl,--gc-sections -Wl,-s`",
        f"- **CRT Baseline (empty.exe):** `{empty_sizes['text']} B` code, `{empty_sizes['rodata']} B` rodata (`{empty_sizes['filesize']} B` on disk)",
        f"- **Snippets Analyzed:** {len(results)}",
        "",
        "### Per-Snippet Code Size Impact",
        "",
        "| Snippet Case | Template .o | Net Linked DCE | Linked .text | Linked .rdata | Diff (Tmpl / Link) | Top Bloat Contributor |",
        "|:---|---:|---:|---:|---:|---:|:---|"
    ]

    tot_tmpl_bytes = 0
    tot_net_linked_sum = 0
    tot_tmpl_base_sum = 0
    tot_linked_base_sum = 0

    for r in results:
        name = r["name"]
        sz = r["sizes"]
        lk = r["linked"]
        top_sym = r["symbols"][0]["name"] if r["symbols"] else "-"
        top_sym_short = (top_sym[:32] + "...") if len(top_sym) > 32 else top_sym

        tot_tmpl_bytes += sz["total"]
        tot_net_linked_sum += lk["net_total"]

        diff_str = "- / -"
        b_info = baseline_data.get("snippets", {}).get(name, {})
        if b_info:
            b_tmpl = b_info.get("template", {}).get("total")
            b_lk = b_info.get("linked", {}).get("net_total")
            d_tmpl_str = "-"
            d_lk_str = "-"
            if b_tmpl is not None:
                tot_tmpl_base_sum += b_tmpl
                d = sz["total"] - b_tmpl
                d_tmpl_str = f"{'+' if d > 0 else ''}{d} B"
            if b_lk is not None:
                tot_linked_base_sum += b_lk
                d = lk["net_total"] - b_lk
                d_lk_str = f"{'+' if d > 0 else ''}{d} B"
            diff_str = f"{d_tmpl_str} / {d_lk_str}"

        print(
            f"{name:<26} "
            f"{sz['total']:>7} B "
            f"{lk['net_total']:>9} B "
            f"{lk['net_text']:>8} B "
            f"{lk['net_rodata']:>9} B "
            f"{diff_str:>18}  "
            f"{top_sym_short}"
        )

        report_lines.append(
            f"| [`{name}`](./{name}.annotated.s) | {sz['total']} B | **{lk['net_total']} B** | "
            f"{lk['net_text']} B | {lk['net_rodata']} B | {diff_str} | `{top_sym_short}` |"
        )

    print("-" * len(header))

    tot_diff_str = "- / -"
    if tot_tmpl_base_sum > 0 or tot_linked_base_sum > 0:
        d_tmpl_tot = tot_tmpl_bytes - tot_tmpl_base_sum
        d_lk_tot = tot_net_linked_sum - tot_linked_base_sum
        tot_diff_str = f"{'+' if d_tmpl_tot > 0 else ''}{d_tmpl_tot} B / {'+' if d_lk_tot > 0 else ''}{d_lk_tot} B"

    print(
        f"{'TOTAL (Isolated Sum)':<26} "
        f"{tot_tmpl_bytes:>7} B "
        f"{tot_net_linked_sum:>9} B "
        f"{'':>10} "
        f"{'':>11} "
        f"{tot_diff_str:>18}"
    )

    # Full Linked Corpus Summary
    c_net_text = corpus_linked["net_text"]
    c_net_rodata = corpus_linked["net_rodata"]
    c_net_tot = corpus_linked["net_total"]
    c_filesize = corpus_linked["raw"].get("filesize", 0)

    c_base = baseline_data.get("corpus", {})
    c_diff_str = ""
    if "linked_net_total" in c_base:
        cdiff = c_net_tot - c_base["linked_net_total"]
        c_diff_str = f" ({'+' if cdiff > 0 else ''}{cdiff} B vs base)"

    print(f"\n================================================================================================================")
    print(f" FULL CORPUS LINKED BINARY (stack_json_corpus_stripped.exe)")
    print(f" Net Code (.text)   : {c_net_text:>6} B")
    print(f" Net ROData (.rdata): {c_net_rodata:>6} B")
    print(f" Net Total Footprint: {c_net_tot:>6} B{c_diff_str}")
    print(f" Total Binary Size  : {c_filesize:>6} B on disk (including CRT & PE headers)")
    print(f"================================================================================================================\n")

    report_lines.extend([
        f"| **TOTAL (Isolated Sum)** | **{tot_tmpl_bytes} B** | **{tot_net_linked_sum} B** | | | **{tot_diff_str}** | |",
        "",
        "### Complete Multi-Snippet Corpus Binary",
        "",
        "When linking all 16 snippets into a single unified binary (`stack_json_corpus_stripped.exe`) with full dead-code elimination (`-Wl,--gc-sections -Wl,-s`), shared library methods and vtables are deduplicated:",
        "",
        f"- **Net Executable Code (`.text`):** `{c_net_text} B`",
        f"- **Net Read-Only Data (`.rdata`):** `{c_net_rodata} B`",
        f"- **Net Total stack_json Footprint:** **`{c_net_tot} B`**{c_diff_str}",
        f"- **Total Executable Size on Disk:** `{c_filesize} B`",
        "",
        "---",
        "",
        "## Detailed Symbol Breakdown per Snippet",
        ""
    ])

    for r in results:
        report_lines.append(f"### [`{r['name']}`](./{r['name']}.annotated.s) — Template .o: {r['sizes']['total']} B | Net Linked: {r['linked']['net_total']} B")
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
    print(f"Annotated assembly files, binaries, and report generated in:\n  {build_dir}\n")


if __name__ == "__main__":
    main()
