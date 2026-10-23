# stack_json Codegen Analysis & Optimization Guide

This document summarizes the goals, methodology, optimization milestones, architectural findings, and future avenues for code generation and binary footprint optimization in `stack_json`.

---

## 1. Objectives & Constraints

- **Primary Goal:** Minimize binary code footprint (`.text`) and read-only data (`.rodata`, primarily vtables and literal tables) generated when compiling parsers and builders with `-Os` (optimized for size).
- **Embedded Target Focus:** Designed for microcontrollers (ESP32, ESP-IDF) where instruction flash and I-cache lines are constrained.
- **Constraints:**
  - Zero breaking changes to the public-facing API (`sjson::json_parser`, `sjson::bind`, `sjson::stack_json`, `sjson::node`, `sjson::node_if`, `sjson::path`, etc.).
  - All unit tests in `espbase_tests.exe` (57 tests across 11 test suites) must pass cleanly.
  - Release compilation uses `-std=gnu++26 -Os -ffunction-sections -fdata-sections -fno-rtti`.
  - Representative measurement via an automated 16-snippet corpus covering typical production usage patterns.

---

## 2. Toolchain & Corpus Methodology

The analysis toolchain is located in this directory:
- **`run_codegen.py`:** Autonomous runner that compiles 16 snippet translation units in `snippets/` using Clang:
  - Generates assembly (`.annotated.s`) with `-fverbose-asm` and `-g` for line-by-line C++ to assembly correlation demangled via `llvm-cxxfilt`.
  - Generates object files (`.o`) and extracts exact `.text` and `.rodata` section bytes via `llvm-objdump -h`.
  - Disassembles and dumps sorted symbols (`.symbols.txt`) per snippet to pinpoint exact bloat contributors.
  - Outputs a consolidated summary report (`codegen_report.md`).
  - Supports `--save-baseline` and `--diff` to verify changes quantitatively.
- **`baseline_sizes.json`:** JSON record of reference snippet sizes.
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

### Phase 5: Deferred Non-Virtual Binding & Loop Hoisting (Current State)
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

### Pass 3: Out-of-Line Scalars, ValueNodeBaseT, and Trivially Copyable StaticPath (9,027 B)
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
- **Result:** Total corpus footprint dropped from **11,729 B to 9,027 B** (**-2,702 B net saving**, -23.0% in this pass, **-58.1% cumulative** from 21,548 B starting baseline).

---

## 4. Current Corpus Summary (9,027 B)

| Snippet Case | Code (.text) | ROData | Total | Net vs Baseline (11,729 B) |
|:---|---:|---:|---:|---:|
| `builder_arrays` | 623 B | 121 B | **744 B** | -318 B |
| `builder_conditional` | 284 B | 108 B | **392 B** | -203 B |
| `builder_nested_doc` | 497 B | 221 B | **718 B** | -504 B |
| `builder_nested_paths` | 293 B | 134 B | **427 B** | -294 B |
| `builder_primitive_multi` | 509 B | 227 B | **736 B** | -293 B |
| `builder_primitive_scalar` | 150 B | 55 B | **205 B** | -37 B |
| `builder_stack_builder` | 291 B | 104 B | **395 B** | -142 B |
| `builder_strings` | 377 B | 184 B | **561 B** | -205 B |
| `parser_dynamic_node` | 399 B | 50 B | **449 B** | +24 B |
| `parser_entity_payload` | 753 B | 214 B | **967 B** | -190 B |
| `parser_nested_paths` | 340 B | 118 B | **458 B** | -84 B |
| `parser_primitive_multi` | 624 B | 195 B | **819 B** | -170 B |
| `parser_primitive_scalar` | 196 B | 46 B | **242 B** | -33 B |
| `parser_strings` | 468 B | 146 B | **614 B** | -156 B |
| `parser_sub_parser` | 679 B | 134 B | **813 B** | -37 B |
| `parser_unknown_catchall` | 401 B | 86 B | **487 B** | -60 B |
| **TOTAL CORPUS** | **6884 B** | **2143 B** | **9027 B** | **-2702 B (-23.0%)** |

---

## 5. Potential Avenues for Further Investigation

### A. Non-Virtual Node Descriptors (VTable Elimination)
- Currently, each instantiation of `ValueNode<StaticPath<N>, T>` and `BindNode<StaticPath<N>, T>` generates a vtable (40 B in `.rodata` under `-fno-rtti`) containing destructor and `emit_value` / `assign` pointers.
- *Investigation:* Could `NodeBase` and `ParseNodeBase` use a plain struct descriptor with a function pointer rather than C++ virtual dispatch?
  ```cpp
  struct NodeDescriptor {
    const PathBase* path;
    void (*emit)(const void* node_data, Buffer& buffer);
  };
  ```
  If nodes with identical payload types share static function pointers without distinct vtables, `.rodata` footprint across multi-node parsers could drop significantly.

### B. Flattening `Document::emit_value` and `Parser::parse`
- In `parser_primitive_scalar` and `builder_primitive_scalar`, inspect whether marking `Parser::parse` or `Document::emit_value` with `__attribute__((always_inline))` allows the compiler to collapse temporary stack frames and eliminate intermediate pointer array allocation entirely for 1-node cases.
- Conversely, for $N > 3$ nodes, investigate an out-of-line helper to construct the `std::span` to reduce stack allocation overhead in the caller.

### C. Compact / Packed Static Path Representation
- `StaticPath<N>` currently stores an array of `std::string_view` (16 bytes each). A depth-3 path requires 48 bytes of storage on the stack.
- *Investigation:* Since path keys are almost exclusively string literals (`const char*`) known at compile time, storing an array of `const char*` plus a single length array or null-separated tokens could reduce node stack footprint by ~50%, improving register allocation and reducing stack frame sizes in deep tree builders.

### D. In-Depth Inspection of `builder_nested_paths`
- `builder_nested_paths` is currently the only snippet larger (+127 B) than its baseline state.
- Inspecting `builder_nested_paths.annotated.s` with the newly added `-g` flag reveals stack spills (`pushq %r15..%rbx`, allocating 512 bytes on the stack).
- Identify whether the temporaries generated by `sjson::node(path(...), val)` can be constructed directly in-place within `std::tuple` using piecewise construction or a builder helper that avoids moving whole nodes.
