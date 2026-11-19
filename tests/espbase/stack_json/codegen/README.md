# stack_json Codegen Analysis & Optimization Guide

This document summarizes the goals, methodology, optimization milestones, architectural findings, and future avenues for code generation and binary footprint optimization in `stack_json`.

---

## 1. Objectives & Constraints

- **Primary Goal:** Minimize binary code footprint (`.text`) and read-only data (`.rodata`, primarily vtables and literal tables) generated when compiling parsers and builders with `-Os` (optimized for size).
- **Embedded Target Focus:** Designed for microcontrollers (ESP32, ESP-IDF) where instruction flash and I-cache lines are constrained.
- **Constraints:**
  - Zero breaking changes to the public-facing API (`sjson::json_parser`, `sjson::bind`, `sjson::stack_json`, `sjson::node`, `sjson::node_if`, `sjson::path`, etc.).
  - All unit tests in `espbase_tests.exe` (63 tests across 12 test suites) must pass cleanly.
  - Release compilation uses `-std=gnu++26 -Os -ffunction-sections -fdata-sections -fno-rtti`.
  - Dual-tier representative measurement via an automated 16-snippet corpus covering typical production usage patterns:
    1. **Isolated Template Call-Site Bloat:** Section bytes generated in snippet object files (`.o`).
    2. **Complete Dead-Code-Eliminated (DCE) Linked Footprint:** Net executable bytes linked with tree-shaken library objects (`-Wl,--gc-sections -Wl,-s`), subtracted against CRT baseline (`empty.exe`).

---

## 2. Toolchain & Corpus Methodology

The analysis toolchain is located in this directory:
- **`run_codegen.py`:** Autonomous runner that compiles 16 snippet translation units in `snippets/` using Clang:
  - Generates assembly (`.annotated.s`) with `-fverbose-asm` and `-g` for line-by-line C++ to assembly correlation demangled via `llvm-cxxfilt`.
  - Generates object files (`.o`) and extracts exact `.text` and `.rodata` section bytes via `llvm-objdump -h`.
  - Disassembles and dumps sorted symbols (`.symbols.txt`) per snippet to pinpoint exact bloat contributors.
  - Pre-compiles library translation units (`buffer.cpp`, `document.cpp`, `parser.cpp`, `pretty_buffer.cpp`, `printer.cpp`) with `-Os -ffunction-sections -fdata-sections -fno-rtti`.
  - Builds a baseline empty executable (`empty.exe`) to measure fixed C-runtime (CRT) overhead.
  - Links individual minimal executables (`<snippet>.linked.exe`) with `-Wl,--gc-sections -Wl,-s` to calculate net application footprint (snippet code + transitively pulled library dependencies).
  - Links a complete multi-snippet corpus binary (`stack_json_corpus_stripped.exe`) measuring real-world deduplication of shared vtables and library helpers across an entire application.
  - Outputs a consolidated summary report (`codegen_report.md`).
  - Supports `--save-baseline` and `--diff` to verify changes quantitatively.
- **`baseline_sizes.json`:** JSON record of reference snippet template sizes and linked DCE footprints.
- **`snippets/`:** 16 self-contained snippet translation units:
  - Builders: `builder_primitive_scalar`, `builder_primitive_multi`, `builder_strings`, `builder_nested_paths`, `builder_conditional`, `builder_arrays`, `builder_nested_doc`, `builder_stack_builder`.
  - Parsers: `parser_primitive_scalar`, `parser_primitive_multi`, `parser_strings`, `parser_nested_paths`, `parser_dynamic_node`, `parser_sub_parser`, `parser_unknown_catchall`, `parser_entity_payload`.

---

## 3. Milestones & Optimization History

### Initial Baseline (Pre-optimization)
- Total 16-snippet corpus: **21,548 B** (`.text`: 15,283 B, `.rodata`: 6,265 B).
- Prominent bloat: Inline `<charconv>` / `std::from_chars` lookup tables (`__itoa::__read`, `__itoa::__pow10_32`), polymorphic `PathBase` / `StaticPath<N>` vtables and destructors, and unpruned RTTI.

### Phase 1: Out-of-Line Scalar Coercion (`dc449ec`)
- **Finding:** Every parser snippet instantiated `<charconv>` and `std::from_chars` inline into the header, duplicating large lookup tables in every translation unit.
- **Solution:** Hoisted all common scalar `coerce_value` overloads (`int`, `uint32_t`, `float`, `double`, `bool`, `std::string`, `std::string_view`, `std::span<char>`) out-of-line into `parser.cpp`, declared in `parser_impl.hpp`.
- **Result:** Eliminated all inline `<charconv>` table bloat.

### Phase 2: Path Devirtualisation (`dc449ec`)
- **Finding:** `PathBase` and `StaticPath<N>` were polymorphic base/derived classes with virtual destructors and virtual `matches_parent` / `get_element`. Every static path of depth $N$ generated its own vtable and thunks.
- **Solution:** Converted `PathBase` into a lightweight, non-virtual view `(const std::string_view* data_, std::size_t size_)`. `StaticPath<N>` became a value type storing `std::array<std::string_view, N>` and inheriting `PathBase`. Replaced `DynamicPathView` in `parser.cpp` with contiguous span views on `path_stack`.
- **Result:** Completely eliminated all `StaticPath` vtables and virtual destructors.

### Phase 3: Alignment with Production Flags (`20bef4e`)
- Added `-fno-rtti` to the codegen analysis toolchain to mirror the ESP-IDF production build configuration.
- **Result:** Removed RTTI descriptors (`typeinfo`), shrinking baseline to **12,541 B**.

### Phase 4: Analysis of Path Devirtualisation Pitfalls (`c21e845`, `904f529`, `c2310c0`)
Two subsequent approaches were attempted to remove virtual lookups from nodes:
1. **The Constructor Bloat Trap (`c21e845`):**
   - Attempted to devirtualize `NodeBase::path()` by storing `const PathBase* path_ptr_` initialized to `&this->path_` in the derived constructor.
   - *Failure mechanism:* Storing a pointer to an internal member made `ValueNode` a **self-referential struct**. Because `stack_json` moves nodes into a `std::tuple`, moving a node changed its address in memory. To re-bind `path_ptr_ = &path_`, custom move and copy constructors were required.
   - Custom move/copy constructors stripped `ValueNode` of being trivially copyable/movable. `std::tuple` was forced to generate non-inline constructor and move functions for every tuple combination, causing net code growth.
2. **The Caching Virtual Dispatch Trap (`904f529`, `c2310c0`):**
   - Introduced `PathBase cached_path_` and `virtual PathBase get_path() const = 0`, populating `cached_path_ = get_path();` inside `reset()`.
   - *Failure mechanism:* Added 16 bytes of storage to every node, added a vtable slot, and on Windows x64 ABI, returning a 16-byte struct required caller stack allocation, passing a hidden pointer in `%rcx`, and copying 16 bytes. Corpus grew to **12,840 B** (+299 B regression).

### Phase 5: Deferred Non-Virtual Binding & Loop Hoisting (`0e0201f`)
- **Deferred `init_node()` for Parser Nodes:**
  - `ParseNodeBase` stores `const PathBase* path_ptr_ = nullptr;`. `path()` is non-virtual (`return *path_ptr_;`).
  - `BindNode` and `DynamicNode` keep `= default` constructors (no custom copy/move logic).
  - When `Parser::parse()` constructs `node_ptrs` on the stationary tuple, it calls `init_node()`:
    ```cpp
    ParseNodeBase* init_node() { this->path_ptr_ = &path_; return this; }
    ```
    This sets `path_ptr_` in a single `leaq` + `movq` at the exact point of traversal with zero constructor bloat.
- **Hoisting Node `reset()` Loop to `parser.cpp`:**
  - Removed `for (auto* n : node_ptrs) n->reset();` from the header template `Parser::parse()`.
  - Moved the reset loop directly into `parse_json_nodes` in `parser.cpp`, eliminating the iteration frame from every caller template instantiation.
- **Lightweight Virtual `path()` on `NodeBase` + Document Cache:**
  - For `NodeBase` (which only ever has `ValueNode`), keeping `virtual const PathBase& path() const override { return path_; }` generates a tiny 4-byte thunk (`leaq 16(%rcx), %rax; retq`) while keeping `Document::nodes_` immutable during `emit()`.
  - In `document.cpp`, cached `const PathBase& n_path = n->path();` inside `emit_level` to eliminate redundant virtual dispatches.
- **Universal Forwarding References:**
  - Updated `node()`, `node_if()`, and `bind()` to take `PathT&&` and `ValueT&&`, eliminating double-copies of `StaticPath`.
- **Result:** Total corpus footprint dropped to **11,729 B** (-1,111 B vs 12,840 B baseline, -45.6% vs original baseline).

### Phase 6: Out-of-Line Scalars, ValueNodeBaseT, and Trivially Copyable StaticPath (`a23f3da`)
- **Out-of-Line Scalar Serialization (`value.hpp` & `document.cpp`):**
  - Declared non-template, out-of-line overloads for all fundamental scalar types (`int`, `unsigned int`, `short`, `unsigned short`, `long`, `unsigned long`, `long long`, `unsigned long long`, `float`, `double`, `bool`, `nullptr_t`, `const char*`, `std::string_view`, `const std::string&`).
  - Constrained generic fallback via C++20 `requires` to only match lambdas and nested Documents.
  - Eliminated inlined `Printer::printx` with varargs overhead, register spills, and literal format strings (`"%lld"`, `"%g"`) from caller translation units.
- **Factored `ValueNodeBaseT<ValueT>` Base Class (`node.hpp`):**
  - Factored `value_` storage and `emit_value(Buffer&)` into `ValueNodeBaseT<ValueT>`.
  - Derived `ValueNode<PathT, ValueT>` now only overrides `path()`. Nodes sharing the same payload type with different paths now share identical `emit_value` code.
- **Trivially Copyable `StaticPath<N>` (`path.hpp`):**
  - Removed inheritance from `PathBase`. Eliminated the self-referential `data_` pointer pointing to member `elements_`.
  - Converted `StaticPath<N>` into a plain value type storing only `std::array<std::string_view, N>`, making it trivially copyable and movable (`std::is_trivially_copyable_v<StaticPath<N>> == true`).
  - Halved the size of `StaticPath<1>` (from 32 bytes to 16 bytes) and reduced all node tuple stack footprints.
  - Replaced non-trivial copy/move constructors on `std::tuple` moves with flat register/stack copies.
- **By-Value `NodeBase::path()` & Direct `ParseNodeBase::path_`:**
  - `NodeBase::path()` now returns `PathBase` (16 bytes, two registers) by value.
  - `ParseNodeBase` stores `PathBase path_{}` directly, populated at initialization by `init_node()`. Completely eliminated `dummy` static object in `UnknownBindNodeBase` and removed pointer-to-pointer indirection when matching parser paths.
- **Array Type Decay in `node()` (`node.hpp`):**
  - Array string literals (`"foo"`, `"bar"`) now decay to `const char*` instead of deducing distinct array references (`char const (&)[N]`).
  - Prevents combinatorial template bloat and duplicate vtables across string literals of differing lengths.
- **Result:** Total isolated template bloat dropped from **11,729 B to 9,027 B** (**-2,702 B net saving**, -23.0% in this pass, **-58.1% cumulative** from 21,548 B starting baseline).

### Phase 7: Dual-Tier Linked DCE Tracking & Library Decomposition (`ea656be`, `cdd68e1`)
- **Dual-Tier Bloat Tracking:** Added tracking of complete, dead-code-eliminated linked binaries (`<snippet>.linked.exe` and `stack_json_corpus_stripped.exe`) against `empty.exe` CRT baseline.
- **Functional Decomposition & Deduplication in `parser.cpp`:**
  - Factored duplicate `if (depth < path_stack.size())` node matching, catch-all handling, and assignment blocks into a unified `assign_value` closure.
  - Extracted shared pointer-based structural brace scanner `find_structural_end(std::string_view)` used by both `next_array_token` and `capture_structural_value()`.
  - Streamlined minified and whitespace-delimited colon checks.
- **Result:** Full corpus linked executable code shrank by **-64 B** (`.text`), net total footprint dropped to **17,460 B** (**-60 B**), and `parser_dynamic_node` linked size shrank by **-76 B**, while benchmark parse throughput increased by **+10% to +40%**.

### Phase 8: Non-Virtual Destructors & Scalar Consolidation (Current State)
- **Non-Virtual Node Base Destructors (`node.hpp`, `parser.hpp`):**
  - Nodes in `stack_json` are automatic stack variables lifetime-managed inside `std::tuple`; they are never allocated on the heap or deleted via base pointers.
  - Replaced `virtual ~NodeBase()` and `virtual ~ParseNodeBase()` with non-virtual protected destructors (`protected: ~NodeBase() = default;`, `protected: ~ParseNodeBase() = default;`).
  - Completely eliminated deleting destructor (`D0`) thunks, complete object destructor entries, and references to `operator delete(void*)`.
  - Shrank all `ValueNode` and `BindNode` vtables by 2 pointer slots (16 bytes on 64-bit, 8 bytes on 32-bit), saving **-857 B** of template bloat across the 16 snippets.
- **Scalar Formatting & Coercion Consolidation (`document.cpp`, `parser.cpp`):**
  - In `document.cpp`, scalar `write_json_value` overloads for `char`, `short`, and `long` route directly through 32-bit (`int`/`unsigned int`) or 64-bit (`long long`/`unsigned long long`) core formatters, eliminating 6 duplicate integer formatting loops.
  - In `parser.cpp`, scalar `coerce_value` overloads for `char`, `short`, and `long` route through core integer coercions, eliminating 9 redundant instantiations of `std::from_chars` and `<charconv>` traits.
  - Replaced the 11-step branching ladder in `num_digits_u64` with a 32-bit fast path and compact loop for >4GB values.
- **Result:**
  - Isolated template bloat dropped from **9,027 B to 8,170 B** (**-857 B**, **-9.5%** in this pass; **-62.1% cumulative** from 21,548 B starting baseline).
  - Full corpus linked binary footprint dropped from **17,460 B to 16,844 B** (**-616 B**).
  - Total binary executable on disk dropped from **32,256 B to 31,744 B** (**-512 B**).
  - Benchmark parse throughput increased to **249.6 MB/s** average (with primitives parse reaching **305 MB/s**).

---

## 4. Current Corpus Summary (8,170 B Template / 16,844 B Linked)

### Dual-Tier Size Breakdown
- **Isolated Template Call-Site Total:** **`8,170 B`** (`6,290 B` code, `1,880 B` rodata)
- **Full Corpus Linked Binary (`stack_json_corpus_stripped.exe`):**
  - Net Code (`.text`): **`13,792 B`**
  - Net Read-Only Data (`.rodata`): **`3,052 B`**
  - Net Total Footprint: **`16,844 B`** (**-616 B** vs prior baseline)
  - Total File on Disk: **`31,744 B`** (including CRT and PE headers, **-512 B** vs prior baseline)

| Snippet Case | Template .o | Net Linked DCE | Linked .text | Linked .rdata | Top Bloat Contributor |
|:---|---:|---:|---:|---:|:---|
| `builder_arrays` | 701 B | **3,736 B** | 3,312 B | 424 B | `sjson::SpanArrayDocument<int const>::emit` |
| `builder_conditional` | 349 B | **3,456 B** | 3,072 B | 384 B | `snippet_builder_conditional` |
| `builder_nested_doc` | 632 B | **3,760 B** | 3,280 B | 480 B | `snippet_builder_nested_doc` |
| `builder_nested_paths` | 384 B | **3,380 B** | 2,992 B | 388 B | `snippet_builder_nested_paths` |
| `builder_primitive_multi` | 651 B | **4,588 B** | 4,016 B | 572 B | `snippet_builder_primitive_multi` |
| `builder_primitive_scalar` | 183 B | **3,172 B** | 2,848 B | 324 B | `snippet_builder_primitive_scalar` |
| `builder_stack_builder` | 352 B | **3,448 B** | 3,072 B | 376 B | `snippet_builder_stack_builder` |
| `builder_strings` | 497 B | **3,428 B** | 2,976 B | 452 B | `snippet_builder_strings` |
| `parser_dynamic_node` | 427 B | **4,988 B** | 3,968 B | 1,020 B | `snippet_parser_dynamic_node` |
| `parser_entity_payload` | 882 B | **5,332 B** | 4,048 B | 1,284 B | `snippet_parser_entity_payload` |
| `parser_nested_paths` | 415 B | **4,528 B** | 3,472 B | 1,056 B | `snippet_parser_nested_paths` |
| `parser_primitive_multi` | 734 B | **5,308 B** | 4,032 B | 1,276 B | `snippet_parser_primitive_multi` |
| `parser_primitive_scalar` | 220 B | **4,336 B** | 3,344 B | 992 B | `snippet_parser_primitive_scalar` |
| `parser_strings` | 550 B | **5,108 B** | 3,920 B | 1,188 B | `snippet_parser_strings` |
| `parser_sub_parser` | 749 B | **5,160 B** | 3,936 B | 1,224 B | `sjson::DynamicNodeBase::parse` |
| `parser_unknown_catchall` | 444 B | **4,600 B** | 3,568 B | 1,032 B | `snippet_parser_unknown_catchall` |
| **TOTAL (Isolated Sum)** | **8,170 B** | **68,328 B** | | | *(Deduplicated corpus: **16,844 B**)* |

---

## 5. Potential Avenues for Further Investigation

### A. Non-Virtual Node Descriptors (Function Pointers vs VTables)
- Currently, each instantiation of `ValueNode<StaticPath<N>, T>` and `BindNode<StaticPath<N>, T>` generates a vtable (now 32 B in `.rodata` under `-fno-rtti`) containing `emit_value` / `assign` and `path()` pointers.
- *Investigation:* Could `NodeBase` and `ParseNodeBase` use a plain struct descriptor with a function pointer rather than C++ virtual dispatch?
  ```cpp
  struct NodeDescriptor {
    PathBase path;
    void (*emit)(const void* node_data, Buffer& buffer);
  };
  ```
  If nodes with identical payload types share static function pointers without distinct vtables, `.rodata` footprint across multi-node parsers and builders could drop significantly.

### B. Direct Child Emission for Flat `Document<Nodes...>`
- For `Document<Nodes...>` where all `Nodes` statically have depth 1 (i.e. `ValueNode<StaticPath<1>, ...>`), evaluating whether calling an inline child emitter directly in `Document::emit(buffer)` can bypass pointer-array construction, `std::span` framing, and `emit_json_nodes` function overhead for the 90%+ case of flat JSON schemas.

### C. Compact / Packed Static Path Representation
- `StaticPath<N>` currently stores an array of `std::string_view` (16 bytes each). A depth-3 path requires 48 bytes of storage on the stack.
- *Investigation:* Since path keys are almost exclusively string literals (`const char*`) known at compile time, storing an array of `const char*` plus a single length array or null-separated tokens could reduce node stack footprint by ~50%, improving register allocation and reducing stack frame sizes in deep tree builders.

### D. In-Depth Inspection of Stack Spills in `builder_nested_paths`
- `builder_nested_paths` has relatively high stack allocation in `builder_nested_paths.annotated.s`.
- Identify whether the temporaries generated by `sjson::node(path(...), val)` can be constructed directly in-place within `std::tuple` using piecewise construction or a builder helper that avoids moving whole nodes.

### E. Out-of-Line Pointer Span Setup
- In both `Document::emit_value` and `Parser::parse`, constructing the array of pointers (`std::array<NodeBase*, N>` and `std::array<ParseNodeBase*, N>`) produces boilerplate at every call site.
- Investigate moving the array construction into an out-of-line helper for $N > 3$ to reduce caller stack frame size.
