#pragma once

/**
 * @file generate.hpp
 * @brief Single-include public API for StackJson JSON generation.
 *
 * @details
 * Include this one header to access the entire StackJson generation surface:
 * buffers, paths, nodes, documents, arrays, builders, and pretty-printing.
 * No `using namespace sjson;` is required; every symbol lives in `namespace sjson`.
 *
 * StackJson is a **zero-heap, zero-copy, strictly-typed** JSON generation library
 * designed for deeply constrained systems (ESP32, RTOS tasks, embedded firmware).
 * Instead of allocating a DOM on the heap it builds a declarative graph of
 * references on the call stack, computes maximum depth requirements at compile
 * time, and achieves perfect zero-copy composition via perfect forwarding.
 *
 * ---
 * ## Quick-start
 *
 * @code{.cpp}
 * #include "espbase/stack_json/generate.hpp"
 *
 * int battery = 98;
 * auto device = sjson::path("device");
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("name",                      "espuck_node_01"),
 *     sjson::node(device("manufacturer"),      "espuck"),
 *     sjson::node(device("power", "battery"),  battery),
 *     sjson::node(device("online"),            true)
 * );
 *
 * sjson::StackBuffer<256> buf;
 * doc.emit(buf);
 * // buf.c_str() →
 * {"name":"espuck_node_01","device":{"manufacturer":"espuck","power":{"battery":98},"online":true}}
 * @endcode
 *
 * ---
 * ## API Reference
 *
 * ### 1. `sjson::path(keys...)`
 *
 * @brief Constructs a compile-time, type-safe JSON object path.
 *
 * @details
 * `path()` returns a `StaticPath<Depth>` whose depth is computed at compile
 * time from the number of string-like arguments supplied.  Calling
 * `operator()(more_keys...)` on a `StaticPath` appends further segments and
 * returns a new, deeper `StaticPath` — enabling fluent chaining that keeps
 * related keys grouped together without paying any runtime cost.
 *
 * Key arguments may be `const char*` literals, `std::string_view`, or lvalue
 * `std::string` variables.  Passing a **temporary** `std::string` is a
 * compile-time error (dangling-reference guardrail).
 *
 * @code{.cpp}
 * // Single-segment root path
 * auto device = sjson::path("device");
 *
 * // Two-segment path: device → origin
 * auto origin = sjson::path("device", "origin");
 *
 * // Chaining with operator() — creates a new StaticPath without modifying device
 * auto power_battery = device("power", "battery");
 *
 * auto doc = sjson::stack_json(
 *     sjson::node(device("manufacturer"), "espuck"),
 *     sjson::node(power_battery,           98),
 *     sjson::node(device("online"),        true)
 * );
 * // → {"device":{"manufacturer":"espuck","power":{"battery":98},"online":true}}
 * @endcode
 *
 * ---
 * ### 2. `sjson::node(path, value)` / `sjson::node(root_key, value)`
 *
 * @brief Creates a key-value node for use inside `stack_json()`.
 *
 * @details
 * The two overloads cover the common cases:
 * - **`node(PathT&&, ValueT&&)`** — accepts any `StaticPath` or expression
 *   convertible to `PathBase` as the key descriptor.
 * - **`node(const char*, ValueT&&)`** — shorthand for a single root-level key;
 *   internally equivalent to `node(path(root_key), value)`.
 *
 * Values are captured by **perfect forwarding**: lvalue references are stored
 * as references (zero copy); arrays decay to pointers.  Any type for which
 * `write_json_value` is defined may be used as a value, including nested
 * `Document` objects, `ArrayDocument`, `SpanArrayDocument`,
 * `StreamArrayDocument`, and printf-style lambdas.
 *
 * @code{.cpp}
 * int temp = 22;
 * auto wifi = sjson::path("wifi");
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("name",           "Kitchen"),          // root key shorthand
 *     sjson::node(wifi("ip"),       "192.168.1.50"),     // path chaining
 *     sjson::node("temperature",    temp),               // lvalue int — zero copy
 *     sjson::node("enabled",        true)                // bool literal
 * );
 * // → {"name":"Kitchen","wifi":{"ip":"192.168.1.50"},"temperature":22,"enabled":true}
 * @endcode
 *
 * ---
 * ### 3. `sjson::node_if(cond, path, value)` / `sjson::node_if(path, const char*)`
 *
 * @brief Creates a node that is conditionally omitted from the output.
 *
 * @details
 * Four overloads are provided:
 * - **`node_if(bool, PathT&&, ValueT&&)`** — emits the node only when `cond`
 *   is `true`; otherwise the key is completely absent from the JSON output.
 * - **`node_if(bool, const char*, ValueT&&)`** — same with a bare string key.
 * - **`node_if(PathT&&, const char* val)`** — null-pointer guard; omits the
 *   node automatically when `val == nullptr`.
 * - **`node_if(const char* root_key, const char* val)`** — same with a bare key.
 *
 * @code{.cpp}
 * struct Config { bool has_icon; const char* icon; const char* category; };
 * Config cfg = { true, "mdi:thermometer", nullptr };
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("command_topic",    "home/cmd"),
 *     // Explicit boolean condition — allows emitting null when false
 *     sjson::node_if(cfg.has_icon, "icon", cfg.icon),
 *     // Implicit null-pointer guard — key is dropped when cfg.category == nullptr
 *     sjson::node_if("entity_category", cfg.category)
 * );
 * // cfg.has_icon == true  → "icon":"mdi:thermometer" is emitted
 * // cfg.category == null  → "entity_category" key is completely absent
 * @endcode
 *
 * ---
 * ### 4. `sjson::stack_json(nodes...)`
 *
 * @brief Assembles a JSON object document from a variadic list of nodes.
 *
 * @details
 * Returns a `Document<Nodes...>` whose nodes are stored in a `std::tuple`
 * by perfect forwarding — no heap allocation occurs.  Calling `.emit(buffer)`
 * traverses the node list, groups consecutive nodes that share a common path
 * prefix, and writes well-formed nested JSON objects.
 *
 * A compile-time `static_assert` ensures every argument is derived from
 * `NodeBase`; passing a raw value instead of a `node()` produces a
 * human-readable build error.
 *
 * Zero-copy composition: outer documents capture inner documents **by
 * reference**, so nesting is essentially free:
 *
 * @code{.cpp}
 * auto wifi_doc = sjson::stack_json(sjson::node("ip",  "192.168.1.50"));
 * auto ble_doc  = sjson::stack_json(sjson::node("mac", "AA:BB:CC:DD:EE"));
 *
 * // root_doc holds references to wifi_doc and ble_doc — no copies
 * auto root_doc = sjson::stack_json(
 *     sjson::node("wifi",      wifi_doc),
 *     sjson::node("bluetooth", ble_doc)
 * );
 *
 * sjson::StackBuffer<256> buf;
 * root_doc.emit(buf);
 * // → {"wifi":{"ip":"192.168.1.50"},"bluetooth":{"mac":"AA:BB:CC:DD:EE"}}
 * @endcode
 *
 * ---
 * ### 5. `sjson::stack_array(elems...)`
 *
 * @brief Constructs a compile-time heterogeneous JSON array.
 *
 * @details
 * Returns an `ArrayDocument<Elements...>` backed by a `std::tuple`.  All
 * element types are deduced at compile time; mixed types (string, int, bool)
 * are supported.  The document can be used as the value of any `node()` or
 * emitted directly via `.emit_value(buffer)`.
 *
 * @code{.cpp}
 * // Homogeneous string array
 * auto caps = sjson::stack_array("wifi", "ble", "touch");
 *
 * // Heterogeneous array
 * auto mixed = sjson::stack_array("v1.2", 42, true);
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("features", caps),
 *     sjson::node("meta",     mixed)
 * );
 * // → {"features":["wifi","ble","touch"],"meta":["v1.2",42,true]}
 * @endcode
 *
 * ---
 * ### 6. `sjson::span_array(container)` / `sjson::span_array(span)`
 *
 * @brief Wraps a contiguous runtime container or `std::span` as a JSON array.
 *
 * @details
 * Two overloads:
 * - **`span_array(Container& c)`** — accepts any type for which `std::data()`
 *   and `std::size()` are defined (`std::vector`, `std::array`, C-arrays).
 *   The element type is deduced automatically.  **Lvalues only** — StackJson
 *   never copies a container.
 * - **`span_array(std::span<ElementT> s)`** — accepts a pre-formed
 *   `std::span` for explicit slice views.
 *
 * Returns a `SpanArrayDocument<ElementT>` that iterates the span at emit time
 * without any intermediate allocation.
 *
 * @code{.cpp}
 * int raw_temps[] = {18, 20, 22, 24};
 * std::span<int> temps(raw_temps);
 *
 * std::vector<float> readings = { 22.5f, 19.0f, 31.2f };
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("supported_temps", sjson::span_array(temps)),
 *     sjson::node("readings",        sjson::span_array(readings))
 * );
 * // → {"supported_temps":[18,20,22,24],"readings":[22.5,19.0,31.2]}
 * @endcode
 *
 * ---
 * ### 7. `sjson::stream_array(callback)`
 *
 * @brief Streaming / generator array — emits elements one at a time via callback.
 *
 * @details
 * Returns a `StreamArrayDocument<Callback>` that invokes `callback(ArrayEmitter&)`
 * at emit time.  The `ArrayEmitter` passed to the callback provides:
 * - **`emit(value)`** — appends one element and inserts the comma separator
 *   automatically; returns `false` if a fixed-size buffer overflows (allowing
 *   early loop termination).
 *
 * This is the right tool when:
 * - Elements are yielded from a sensor loop, database cursor, or generator.
 * - You cannot (or do not want to) hold all elements in memory simultaneously.
 * - You want to nest `stack_json` objects inside the array.
 *
 * @code{.cpp}
 * // 1. Generator loop — 100 multiples of 10
 * auto doc = sjson::stack_json(
 *     sjson::node("device",   "puck_01"),
 *     sjson::node("readings", sjson::stream_array([&](auto& stream) {
 *         for (int i = 0; i < 100; ++i) {
 *             if (!stream.emit(i * 10)) break; // early exit on buffer full
 *         }
 *     }))
 * );
 *
 * // 2. Inline lambda (ArrayEmitter& overload)
 * auto status_doc = sjson::stack_json(
 *     sjson::node("status", "ok"),
 *     sjson::node("items",  [&](sjson::ArrayEmitter& stream) {
 *         stream.emit("alpha");
 *         stream.emit("beta");
 *     })
 * );
 *
 * // 3. Nested stack_json objects — zero extra allocation
 * auto telemetry = sjson::stream_array([&](auto& stream) {
 *     while (sensor.has_data()) {
 *         auto s = sensor.read();
 *         if (!stream.emit(sjson::stack_json(
 *                 sjson::node("temp", s.temp),
 *                 sjson::node("hum",  s.hum)))) {
 *             break; // Buffer full — stop early
 *         }
 *     }
 * });
 * sjson::StackBuffer<1024> buf;
 * telemetry.emit(buf);
 * @endcode
 *
 * ---
 * ### 8. `sjson::StackBuffer<N>`
 *
 * @brief Primary stack-allocated output buffer with a fixed capacity of `N` bytes.
 *
 * @details
 * `StackBuffer<N>` is the go-to output buffer for most embedded use cases.  It
 * is a concrete subclass of `sjson::Buffer` that allocates exactly `N + 1`
 * bytes on the stack (`+1` for a null terminator).  Writing beyond capacity
 * sets the buffer's `bad()` flag and returns 0 — it **never overflows**.
 *
 * Key methods:
 * - **`emit(doc)`** / **`doc.emit(buf)`** — render a document into the buffer.
 * - **`c_str()`** — null-terminates and returns a `const char*` pointer.
 * - **`view()`** — returns a `std::string_view` without null-termination.
 * - **`length()`** — current byte count.
 * - **`reset()`** — clears content and resets the bad flag.
 * - **`bad()`** — `true` if any write was truncated.
 *
 * @code{.cpp}
 * sjson::StackBuffer<256> buf;
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("device_class", "temperature"),
 *     sjson::node("unit",         "°C")
 * );
 * doc.emit(buf);
 *
 * if (!buf.bad()) {
 *     mqtt_publish(buf.c_str());
 * }
 * @endcode
 *
 * ---
 * ### 9. `sjson::DynamicBuffer<Container, ExpandSize>`
 *
 * @brief Heap / PSRAM growing buffer for documents that exceed stack limits.
 *
 * @details
 * `DynamicBuffer` is a concrete subclass of `sjson::Buffer` backed by any
 * STL-compatible container (default: `std::vector<char, PSRAMAllocator<char>>`
 * which routes to ESP32 SPIRAM/PSRAM).  The buffer grows automatically as
 * content is written and always maintains at least `ExpandSize` (default 256)
 * bytes of contiguous spare capacity for zero-copy printf callbacks.
 *
 * Template parameters:
 * - **`Container`** — the backing storage type (must expose `.data()`,
 *   `.size()`, `.resize()`).  Defaults to `std::vector<char, PSRAMAllocator<char>>`.
 * - **`ExpandSize`** — minimum spare capacity maintained for in-place
 *   expansion; defaults to 256.
 *
 * Key methods mirror `StackBuffer`: `c_str()`, `view()`, `length()`, `reset()`,
 * `bad()`, plus **`container()`** to `std::move` out the backing storage.
 *
 * @code{.cpp}
 * // Default — routes to PSRAM via PSRAMAllocator
 * sjson::DynamicBuffer<> psram_buf;
 *
 * // Explicit std::vector on the standard heap
 * sjson::DynamicBuffer<std::vector<char>> heap_buf;
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("device_class", "temperature"),
 *     sjson::node("state_topic",  [&](auto& print) {
 *         // Buffer guarantees ≥256 bytes for this expansion — no realloc during write
 *         print("home/sensor/%s/state", config.identifier);
 *     })
 * );
 *
 * doc.emit(psram_buf);
 * mqtt_client.publish(psram_buf.c_str());
 * @endcode
 *
 * ---
 * ### 10. `sjson::PrettyBuffer`
 *
 * @brief Decorator that adds human-readable indentation to any `sjson::Buffer`.
 *
 * @details
 * `PrettyBuffer` wraps an existing `sjson::Buffer` (typically a `StackBuffer`
 * or `DynamicBuffer`) and intercepts every `write()` call to insert newlines
 * and spaces as the JSON object/array nesting depth changes.  It is completely
 * decoupled from the generation tree: if you never construct a `PrettyBuffer`,
 * the compiler's dead-code elimination removes all pretty-printing code from
 * the binary at zero cost.
 *
 * Constructor: `PrettyBuffer(Buffer& dest, uint8_t indent_size = 2)`
 *
 * @code{.cpp}
 * sjson::StackBuffer<512> raw;
 * sjson::PrettyBuffer pretty(raw, 2); // 2 spaces per indent level
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("name",   "espuck"),
 *     sjson::node("online", true)
 * );
 * doc.emit(pretty);
 *
 * // raw.c_str() now contains:
 * // {
 * //   "name": "espuck",
 * //   "online": true
 * // }
 * @endcode
 *
 * ---
 * ### 11. `sjson::StackBuilder<N>` / `sjson::Builder`
 *
 * @brief Multi-document merger for virtual-dispatch and cross-boundary patterns.
 *
 * @details
 * The template system that makes StackJson zero-cost also makes it hard to
 * pass documents across function boundaries or through virtual methods — every
 * `Document<...>` is a unique type.  `Builder` / `StackBuilder<N>` escape the
 * template matrix by collecting node **pointers** into a fixed-size flat array
 * on the stack, then emitting them as a single merged JSON object.
 *
 * - **`Builder`** — the non-templated base class holding a `std::span<NodeBase*>`.
 *   Pass `Builder&` across function and virtual-method boundaries.
 * - **`StackBuilder<N>`** — concrete subclass that owns a `std::array<NodeBase*, N>`
 *   on the stack.  `N` is the maximum total number of **nodes** (not documents)
 *   that can be collected.
 *
 * Key methods:
 * - **`add(Document<...>& doc)`** — appends all nodes from `doc` into the
 *   builder's flat array.  Returns `Builder&` for chaining.
 * - **`emit(Buffer&)`** — renders all collected nodes as one merged JSON object.
 *
 * @code{.cpp}
 * // Interface that injects nodes into an existing builder
 * void Entity::inject_into(sjson::Buffer& buf, sjson::Builder& builder) const {
 *     auto base_doc = sjson::stack_json(sjson::node("unique_id", this->id));
 *     builder.add(base_doc);
 *     builder.emit(buf);
 * }
 *
 * // Caller — space for up to 32 node pointers on the stack
 * sjson::StackBuffer<512> buf;
 * sjson::StackBuilder<32> builder;
 *
 * entity.inject_into(buf, builder);
 * // buf.c_str() → {"unique_id":"..."}
 * @endcode
 */

#include "espbase/stack_json/array_emitter.hpp"
#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/builder.hpp"
#include "espbase/stack_json/document.hpp"
#include "espbase/stack_json/dynamic_buffer.hpp"
#include "espbase/stack_json/node.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/pretty_buffer.hpp"
#include "espbase/stack_json/printer.hpp"
#include "espbase/stack_json/value.hpp"
