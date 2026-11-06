# stack_json Performance Profiling & Optimization Plan

## 1. Overview & Context

Following the implementation of `stream_array(...)` and `ArrayEmitter`, the benchmark suite recorded a measurable regression in build and parse throughput across several workloads (e.g., `mixed_iot` build dropped ~23%, `strings` build dropped ~17%).

### Key Objectives
1. **Profile Call Trees & Flamegraphs:** Identify which functions dominate execution time during streaming generation and parsing using `samply`.
2. **Analyze Line-by-Line Hotspots:** Pinpoint exact source lines and branch frequencies executed millions of times using `llvm-profdata` and `llvm-cov`.
3. **Execute Targeted Optimizations:** Improve throughput without increasing binary size or sacrificing the clean declarative API.
4. **Automate in `run_perf.py`:** Add one-command profiling workflows directly into the Python runner.

---

## 2. Verified Tooling Environment

| Tool | Path / Location | Purpose |
|:---|:---|:---|
| **`samply`** | `C:\msys64\clang64\bin\samply.exe` | High-frequency kernel ETW sampling, call stack unwinding, interactive browser flamegraphs. |
| **`llvm-cov`** | `C:\msys64\clang64\bin\llvm-cov.exe` | Line-by-line source execution count and branch analysis. |
| **`llvm-profdata`**| `C:\msys64\clang64\bin\llvm-profdata.exe` | Merging and indexing raw LLVM profile data (`.profraw` $\rightarrow$ `.profdata`). |
| **`clang++`** | `C:\msys64\clang64\bin\clang++.exe` | Primary compiler (`-std=gnu++26 -O3 -DNDEBUG`). |
| **`run_perf.py`** | `tests/espbase/stack_json/perf/run_perf.py` | Benchmark runner and regression tracker. |

---

## 3. Leading Hypotheses for the Regression

1. **Non-Inlined `ArrayEmitter::before_element()` Cross-TU Call:**
   - Every array element calls `out.emit(...)` $\rightarrow$ `before_element()`.
   - `before_element()` is currently compiled out-of-line in `document.cpp`. For 1,000,000+ elements per run, this adds an unconditional cross-TU function call, a virtual function dispatch (`buffer_.write(",")`), and a status check (`buffer_.bad()`).
2. **Intermediate `Document` Setup Overhead:**
   - `out.emit(stack_json(...))` forwards an rvalue `Document` to `write_json_value(Buffer&, T&&)`, which executes `Document::emit_value`.
   - For every single element, `Document::emit_value` packs `std::array<NodeBase*, N>` on the stack and dispatches into `emit_json_nodes`.
3. **Devirtualization Opportunities:**
   - In the previous manual loop, the compiler could inline buffer writes directly into the concrete `FastBenchmarkBuffer`.
   - `ArrayEmitter` holds an abstract `Buffer&`, forcing every comma and leaf write through virtual function tables.

---

## 4. Phase 1: Flamegraph Profiling with `samply`

`samply` records native Windows ETW CPU sampling at high frequency and serves the interactive Firefox Profiler UI locally.

### Step 1: Ensure Frame Pointers & Debug Symbols in CMake
Ensure `stack_json_perf` has frame pointers preserved so ETW can walk call stacks cleanly:
```cmake
target_compile_options(stack_json_perf PRIVATE -O3 -g -fno-omit-frame-pointer -DNDEBUG)
```

### Step 2: Record a Baseline Flamegraph
Run `samply` on the full benchmark suite:
```powershell
samply record .\tests\build\stack_json_perf.exe --mb 100 --iterations 3
```

### Step 3: Record an Isolated Workload Trace
To analyze a single workload without noise from other schemas:
```powershell
# Profile only the mixed IoT payload
samply record .\tests\build\stack_json_perf.exe --mb 100 --iterations 3 --filter mixed_iot

# Profile only string escaping & text
samply record .\tests\build\stack_json_perf.exe --mb 100 --iterations 3 --filter strings
```

### Step 4: Exporting Profiles Offline
To save the profile for offline comparison or archiving:
```powershell
samply record --save-only tests/build/perf/mixed_iot.json.gz .\tests\build\stack_json_perf.exe --mb 100 --iterations 3 --filter mixed_iot
```
You can load `mixed_iot.json.gz` anytime at [profiler.firefox.com](https://profiler.firefox.com/) or inspect it locally with `samply load`.

### Key Views to Inspect in `samply`
1. **Flame Graph Tab:** Visual horizontal stack frames. Look for wide bars—these represent the biggest time sinks.
2. **Call Tree Tab (Inverted / Bottom-Up):** Sorts functions by "Self Time" (time spent executing inside the function body rather than callees).
3. **Marker Chart:** Tracks heap allocations or thread switches (if any).

---

## 5. Phase 2: Line-Level Hotspot Analysis with `llvm-cov`

While `samply` shows the call tree, LLVM source instrumentation reveals the exact lines inside `document.cpp` and `parser.cpp` where cycles are burned.

### Step 1: Compile with Source Instrumentation
Compile `stack_json_perf` with Clang's source profiler:
```powershell
C:\msys64\clang64\bin\clang++.exe -std=gnu++26 -O3 -g -fno-omit-frame-pointer `
  -fprofile-instr-generate -fcoverage-mapping -DNDEBUG `
  -I src -I emulated/esp-idf/include `
  tests/espbase/stack_json/perf/perf_main.cpp `
  src/espbase/stack_json/buffer.cpp `
  src/espbase/stack_json/document.cpp `
  src/espbase/stack_json/parser.cpp `
  src/espbase/stack_json/pretty_buffer.cpp `
  src/espbase/stack_json/printer.cpp `
  -o tests/build/perf/stack_json_perf_instrumented.exe
```

### Step 2: Run and Collect `.profraw`
```powershell
$env:LLVM_PROFILE_FILE = "tests/build/perf/hotspots.profraw"
.\tests\build\perf\stack_json_perf_instrumented.exe --mb 50 --iterations 1
```

### Step 3: Index and Generate Source Report
```powershell
# 1. Merge raw profile
C:\msys64\clang64\bin\llvm-profdata.exe merge -sparse tests/build/perf/hotspots.profraw -o tests/build/perf/hotspots.profdata

# 2. View hot lines in terminal for document.cpp
C:\msys64\clang64\bin\llvm-cov.exe show .\tests\build\perf\stack_json_perf_instrumented.exe `
  "--instr-profile=tests/build/perf/hotspots.profdata" `
  src/espbase/stack_json/document.cpp

# 3. Export full browsable HTML report
C:\msys64\clang64\bin\llvm-cov.exe show .\tests\build\perf\stack_json_perf_instrumented.exe `
  "--instr-profile=tests/build/perf/hotspots.profdata" `
  --format=html -output-dir=tests/build/perf/html_hotspots
```
Open `tests/build/perf/html_hotspots/index.html` in Chrome/Edge to browse lines highlighted with exact execution counts.

---

## 6. Phase 3: Automation in `run_perf.py`

Extend `tests/espbase/stack_json/perf/run_perf.py` with the following flags:

```python
# Proposed CLI options:
parser.add_argument("--samply", action="store_true",
                    help="Run benchmark under samply and open interactive browser flamegraph")
parser.add_argument("--hotspots", action="store_true",
                    help="Compile with LLVM instrumentation and generate line-by-line hotspot HTML report")
parser.add_argument("--save-trace", type=Path, default=None,
                    help="Path to save samply .json.gz profile archive")
```

When `--samply` is passed:
1. Verify `samply` is on PATH (`where.exe samply` or `C:\msys64\clang64\bin\samply.exe`).
2. Run `samply record [args]`.
3. If `--save-trace` is supplied, save to disk.

When `--hotspots` is passed:
1. Rebuild with `-fprofile-instr-generate -fcoverage-mapping`.
2. Execute with `LLVM_PROFILE_FILE`.
3. Invoke `llvm-profdata merge` and `llvm-cov show --format=html`.
4. Print summary of the top 10 most heavily executed lines of code.

---

## 7. Optimization Candidates to Evaluate Post-Profiling

Based on profiling findings, prioritize testing these optimizations:

### 1. Inlining `before_element()`
- Move `bool before_element()` into `array_emitter.hpp` as `inline`.
- Replace the out-of-line call with:
  ```cpp
  inline bool ArrayEmitter::before_element() {
    if (buffer_.bad()) return false;
    if (!first_) return buffer_.write(",") != 0;
    first_ = false;
    return true;
  }
  ```
- Measure whether eliminating the cross-TU call overhead recovers the 10-15% throughput difference.

### 2. Direct Child Node Emission in `Document::emit_value`
- For simple 1-level documents where all nodes share depth 1, evaluate bypassing `emit_json_nodes` tree traversal and directly looping over `(n.emit_value(buffer), ...)`.

### 3. Buffer Fast-Path Devirtualization
- Add a non-virtual inline `write_fast(std::string_view)` or template specialization when the buffer type is known to avoid virtual function table dispatches on short tokens (`","`, `":"`, `"{"`, `"}"`).

### 4. Regression Verification
- After each optimization, verify:
  1. `espbase_tests.exe` (all 63 tests pass).
  2. `python run_codegen.py` (0 B added template bloat).
  3. `python run_perf.py` (measure exact MB/s improvement against baseline).

---

## 8. Quick Commands Cheatsheet

```powershell
# 1. Build benchmark with debug symbols and frame pointers
ninja -C tests/build stack_json_perf

# 2. Run samply interactive flamegraph
samply record .\tests\build\stack_json_perf.exe --mb 100 --iterations 3

# 3. Run samply on a single workload
samply record .\tests\build\stack_json_perf.exe --mb 100 --iterations 3 --filter mixed_iot

# 4. Standard benchmark run with diff vs baseline
python tests/espbase/stack_json/perf/run_perf.py --mb 100

# 5. Save updated baseline after optimizations
python tests/espbase/stack_json/perf/run_perf.py --save-baseline

# 6. Verify codegen size impact
python tests/espbase/stack_json/codegen/run_codegen.py
```

