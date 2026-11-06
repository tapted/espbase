# stack_json Performance Profiling & Optimization Plan

## 1. Overview & Context

Following the implementation of `stream_array(...)` and `ArrayEmitter`, we established a rigorous performance engineering workflow to profile, measure, and optimize in-memory throughput across 5 representative workloads (`primitives`, `floats`, `strings`, `nested_paths`, `mixed_iot`) without sacrificing binary size, safety, or the clean declarative API.

---

## 2. Tooling & Automation Setup (Achieved)

| Tool | Integration | Purpose |
|:---|:---|:---|
| **`samply`** | `--samply`, `--save-trace` in `run_perf.py` | ETW kernel sampling and interactive browser flamegraph visualization. |
| **PDB Symbols** | `-gcodeview -Wl,--pdb=...` in `CMakeLists.txt` | Emits full CodeView PDB debug symbols on Windows so `samply` and Firefox Profiler can resolve exact function names, namespaces, and source lines. |
| **`llvm-cov` / `llvm-profdata`** | `--hotspots` in `run_perf.py` | Source-level execution counting producing browsable line-by-line HTML reports. |
| **Benchmark Suite** | `run_perf.py` | Automated in-memory benchmarking across 5 workloads with diffing vs baseline. |
| **Codegen Inspector** | `run_codegen.py` | Verifies 0 B template bloat on embedded targets across 16 size-optimized snippets. |

### Profiling Workflows
```powershell
# 1. Run standard performance benchmark suite
python tests/espbase/stack_json/perf/run_perf.py --mb 100 --iterations 3

# 2. Record interactive flamegraph under samply (requires Admin terminal on Windows)
python tests/espbase/stack_json/perf/run_perf.py --samply --filter primitives

# 3. Generate line-level HTML hotspot report
python tests/espbase/stack_json/perf/run_perf.py --hotspots --mb 10 --iterations 1 --filter primitives
# Report generated at: tests/build/perf/html_hotspots/index.html
```

---

## 3. Profiling Findings & Root Causes Identified

### From `samply` ETW Trace (`trace.json.gz`)
* **18% in `std::isspace`:** The C standard library `<cctype>` `std::isspace` was being invoked on every single digit of every number during primitive scanning. Because `std::isspace` includes locale checks, it was dominating parser CPU cycles.
* **7.5% in `DynamicNodeBase::next_array_token`:** Array-of-object scanning in `items >> obj_str` was using index-based character lookups with repeated bounds checks.
* **2% in `memcpy` within `write_signed`:** Formatting integers backwards into a temporary stack buffer and then calling `std::memcpy` caused 66% of `write_signed` time to be spent moving memory.

### From LLVM Source Coverage (`--hotspots`)
* **Cross-TU Calls on Array Emission:** `ArrayEmitter::before_element()` was compiled out-of-line in `document.cpp`, causing an external function call, virtual call, and state check for every element in million-object loops.
* **Redundant Tree Traversal on Flat Objects:** For depth-1 objects (e.g. `primitives`, `strings`, `floats`), `emit_json_nodes` was resetting node state (`n->reset()`), constructing `PathView`, and checking `matches_parent()` across all nodes repeatedly.
* **Key Emission Virtual Dispatches:** Emitting `,"key":` resulted in up to 5 separate virtual `buffer.write()` dispatches per key (`","`, `"\""`, `"key"`, `"\""`, `":"`).
* **Linear Parser Key Scans:** In `parser.cpp`, binding matching continued iterating through all nodes even after a match was already found.

---

## 4. Optimizations Implemented & Verified

### 1. Inlined `ArrayEmitter::before_element()` and `ok()`
* Moved `before_element()` and `ok()` from `document.cpp` into `array_emitter.hpp` as `inline`.
* Enables the compiler to inline comma generation directly into caller streaming loops.

### 2. Flat Object Fast-Path in `emit_json_nodes()`
* Added a fast single-pass depth check in `document.cpp`.
* For flat objects (where all nodes have depth 1), all recursive tree traversals, node resets, `PathView` instantiations, and `matches_parent` checks are bypassed entirely.
* At the root level (`c_depth == 0`) of nested schemas, `matches_parent()` is also skipped.

### 3. Lightweight Zero-Allocation Direct Integer Formatting
* Avoided `std::to_chars` for floating-point numbers to protect microcontrollers (ESP32) from massive table bloat (Ryu/Dragon4). Floats continue using `Printer::printx(buffer, "%g", ...)` via `vsnprintf`.
* Replaced format-string parsing for integers with an in-place reverse base-10 digit generator.
* Replaced the temporary stack buffer and `std::memcpy` with `num_digits` pre-calculation, writing digits directly into `span.data()`:
  - 0 heap allocations
  - 0 intermediate `memcpy` calls
  - Fully safe for 64-bit integers and `INT64_MIN` without UB

### 4. Fast RFC 8259 Whitespace & Primitive Delimiter Scanning
* Replaced `std::isspace` with `is_json_whitespace(char c)`, checking strictly the 4 characters defined by RFC 8259 (`' '`, `'\t'`, `'\n'`, `'\r'`), eliminating locale checks and table lookups.
* Implemented `is_primitive_delim(char c)` using a `switch`, which Clang compiles to a single branchless bit-test (`bt`) instruction.

### 5. Pointer-Based `next_array_token` Object Slicing
* Replaced indexed lookups in `DynamicNodeBase::next_array_token` with pointer arithmetic (`const char* ptr`), significantly speeding up object slicing when iterating arrays of objects.

### 6. Fast Colon Check & Early-Exit Node Matching
* In `parser.cpp`, added a direct check for `:` following keys, avoiding `skip_ws_and_comments()` in minified JSON.
* Added `break;` upon finding a matching parse node, reducing node comparisons by over 60%.

### 7. Fast-Path `Buffer::write_escaped`
* Added a fast check in `Buffer::write_escaped`: if a string/key contains no control characters or quotes, quotes and content are committed directly to `get_write_span()` in a single operation, eliminating multiple virtual calls per key.

---

## 5. Status & Verification Results

* **Unit Tests:** 100% pass rate (**63 / 63** tests passing in `espbase_tests.exe`).
* **Code Size Impact:** **0 Bytes** added template bloat across all 16 snippets in `run_codegen.py`.
* **Symbol Resolution:** Working cleanly in `samply` via generated PDB.

---

## 6. Next Optimization Avenues to Explore

1. **Direct Child Emission at Compile-Time for `Document`:**
   - For `Document<Nodes...>` where all `Nodes` statically have depth 1 (i.e. `ValueNode<StaticPath<1>, ...>`), evaluate calling an inline child emitter directly to bypass `emit_json_nodes` function calls.
2. **Key-Value String Writing Consolidation:**
   - For known depth-1 keys, combine `","` + `"` + `key` + `":"` into a single span write before writing the value.
3. **Parse Node Hashing / Direct Dispatch for Large Schemas:**
   - For schemas with many keys (>8 keys), evaluate small perfect-hash or first-char switch dispatch to avoid linear scanning of parse nodes.
