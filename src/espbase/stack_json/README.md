# StackJson 🧱

**A zero-heap, zero-copy, strictly-typed C++26 JSON library for deeply constrained systems.**

StackJson was built for environments where dynamic memory allocation is a liability—like generating complex MQTT discovery payloads on an ESP32 or parsing deeply nested configuration data inside an RTOS task. 

Instead of allocating a DOM (Document Object Model) on the heap, StackJson uses C++26 template metaprogramming to build a declarative graph of references on your call stack. It computes maximum stack depth requirements at compile time, safely handles Unicode, and achieves perfect zero-copy composition.

---

## ⚡ Motivation

Modern C++ JSON libraries (like `nlohmann::json` or `ArduinoJson`) are fantastic, but they rely on dynamic memory. On microcontrollers, constantly allocating and freeing JSON objects leads to memory fragmentation, heap exhaustion, and eventually, a hard crash.

StackJson flips the model:
1. **You own the memory:** All data lives in your local variables. 
2. **The compiler does the work:** Stack depths and node relationships are computed at compile time.
3. **Zero copies:** Documents capture your variables by reference using perfect forwarding. Update your variables *after* building the document, and the emitted JSON reflects the live changes.

---

## 🛠️ Usage Examples: Generation

StackJson allows you to declaratively build JSON payloads that mirror the final output structure.

### The Basics
Paths can be defined inline or stored as variables for clean grouping.

```cpp
#include <stack_json.hpp>

using namespace sjson;

int main() {
    int battery = 98;
    auto device = path("device");

    auto doc = stack_json(
        node("name", "espuck_node_01"),
        node(device("manufacturer"), "espuck"),
        node(device("power", "battery"), battery), // Deeply nested path
        node(device("online"), true)
    );

    StackBuffer<256> buffer;
    doc.emit(buffer);
    
    // buffer.c_str() now contains a null-terminated JSON string!
    // {"name":"espuck_node_01","device":{"manufacturer":"espuck","power":{"battery":98},"online":true}}
}

```

### Conditional Nodes

Need to omit keys if they aren't configured? Use `node_if`. If the condition is false, the key is completely excluded from the output.

```cpp
auto btn_doc = stack_json(
    node("command_topic", "home/cmd"),
    // conditional => nulls can be output
    node_if(config.has_icon, "icon", config.icon),
    // no conditional => nulls drop implicitly
    node_if("entity_category", config.category)
);

```

### Arrays and Spans

StackJson handles arrays of primitive types, nested objects, or even runtime spans.

```cpp
// Compile-time arrays
auto capabilities = stack_array("wifi", "ble", "touch");

// Runtime arrays (wraps a std::span without allocating!)
int raw_temps[] = {18, 20, 22, 24};
std::span<int> temps(raw_temps);

auto doc = stack_json(
    node("features", capabilities),
    node("supported_temps", span_array(temps))
);

```

### Zero-Copy Composition (Perfect Forwarding)

Because StackJson uses perfect forwarding, assembling a master document from smaller components consumes almost no stack space. The master document simply holds references to the local components.

```cpp
auto wifi_doc = stack_json(node("ip", "192.168.1.50"));
auto ble_doc = stack_json(node("mac", "AA:BB:CC:DD:EE"));

// Outer document captures the inner documents by reference!
auto root_doc = stack_json(
    node("wifi", wifi_doc),
    node("bluetooth", ble_doc)
);

```

### The Non-Templated Builder

Need to pass documents across function boundaries or virtual methods? Use `StackBuilder` to escape the template matrix and merge documents safely on the stack.

```cpp
void Entity::inject_into(Buffer& buffer, BuilderBase& builder) const {
    auto base_doc = stack_json(node("unique_id", this->id));
    
    builder.add(base_doc);
    builder.emit(buffer); // Evaluates all collected documents and writes!
}

// In the caller:
// Space for up to 32 keys (just pointers on the stack).
StackBuilder<32> builder;
inject_into(buffer, builder);

```

---

## 🔍 Usage Examples: Parsing

StackJson's parser is a zero-allocation SAX engine. You bind your local variables to expected JSON paths, and the parser drops the values right into your variables as it scans.

### Type Coercion and Binding

```cpp
std::string_view json = R"({"state": "ON", "brightness": "50"})";

std::string_view state;
int brightness = 0; // The parser will auto-convert the string "50" to an int!

auto parser = json_parser(
    bind("state", state),
    bind("brightness", brightness)
);

parser.parse(json);

```

### Tri-State Null Tracking

In IoT, distinguishing between an omitted key and an explicit `null` (clear state) is critical. StackJson handles this natively.

```cpp
std::string_view json = R"({"brightness": null})";

int brightness = 100;
auto brightness_node = bind("brightness", brightness);
auto parser = json_parser(brightness_node);
parser.parse(json);

if (brightness_node.was_set()) {
    if (brightness_node.was_null()) {
        // Explicitly set to null!
        // (nulls assign default-initalized values to bindings)
    } else {
        // Set to a new value.
    }
} else {
  // no match => bindings are untouched.
}

```

### String Decoding Strategies

Depending on your memory constraints, you can choose how to handle escaped strings (`\n`, `\"`):

1. **`std::string_view`**: Zero-copy. You get a view directly into the raw, unparsed JSON buffer (escape characters remain intact).
2. **`std::span<char>`**: Provide your own stack buffer. StackJson decodes the string into your buffer and safely shrinks the span to the exact decoded length.
3. **`std::string`**: The pragmatic escape hatch. StackJson will dynamically resize the heap string to perfectly fit the decoded characters with exactly one allocation.

---

## 📊 Feature Support Matrix

### 🟢 Fully Supported (RFC 8259 Compliant)

* **Zero-Allocation Traversal:** Both generating and parsing operate completely without the heap.
* **Compile-Time Depth Calculation:** Parser bounds checking perfectly auto-sizes to the deepest path requested.
* **UTF-8 / Unicode:** High-byte characters (like `°C` or emojis) pass through transparently without triggering aggressive ASCII-escaping bugs.
* **Nested Objects & Paths:** Limitless object nesting within the boundaries of your RTOS stack.
* **Auto Type Coercion:** Parser seamlessly converts strings to numbers/booleans and vice versa.
You make a very fair point. When you optimize relentlessly for zero-allocation and binary size, you inherently leave some standard JSON library features on the cutting room floor.

Let’s be brutally honest in the README about what we traded away to get this performance, where we slightly cheat the RFC 8259 spec, and what extensions would be genuinely useful for an ESP32 environment.

Here is an expanded breakdown you can add to the README to give it that battle-tested, transparent feel.

---

### 🟡 On the Roadmap (TODO & Extensions)

* **Array Parsing:** Currently, extracting specific indices from incoming arrays (e.g., `bind(path("options", 0), target)`) requires an engine update.

* **Decoding `\uXXXX` Sequences:** Our `decode_json_string` helper perfectly unescapes basic control characters (`\n`, `\"`, `\t`), but it currently skips decoding full Unicode hex sequences into UTF-8 bytes.
* **JSON5 / JSONC Support (Extensions):**

  * **Trailing Commas:** Standard JSON strictly forbids them, but human-written configs (and Home Assistant sometimes) often leave them in. Our parser currently strictly expects a key or `]` / `}`.

  * **Comments:** Skipping `//` and `/* */` during parsing is highly desirable for IoT configuration files.

  * **Unquoted Keys / Single Quotes:** Parsing relaxed JSON5 syntax would make the SAX engine much more forgiving for handwritten payloads.


* **NDJSON (Newline Delimited JSON):** Implementing a streaming reset so the parser can continuously evaluate a stream of back-to-back JSON objects over a serial or TCP socket.

### 🔴 Out of Scope / Infeasible (The Trade-offs)

Because StackJson operates entirely on the stack and refuses to allocate dynamic memory, we cannot support the following features found in libraries like `nlohmann::json` or `ArduinoJson`:

* **Unknown Key Iteration:** We cannot iterate over keys we don't know about. If you don't bind a `path("unknown_key")` at compile time, the parser completely ignores it. You cannot ask StackJson, "give me a list of all keys in this object."

* **Full DOM Manipulation:** We will never load an arbitrary JSON tree into a mutable in-memory DOM. You cannot parse a document, alter a specific nested node, and then re-emit it. (Though our `operator+` document merging achieves a similar result for generation).

* **Perfect Float Round-Tripping:** Desktop libraries drag in massive algorithms (like Ryu or Dragonbox, costing up to 150kB of binary size) to guarantee that parsing and re-emitting a float produces the exact same decimal representation. To save space, we fall back to `<charconv>` and `snprintf`, which are fast and small but may slightly alter float string representations (e.g., `3.14` might emit as `3.1400000000000001`).

* **Duplicate Key Detection:** The JSON spec states that behavior regarding duplicate keys is "unpredictable." If a payload contains `{"a": 1, "a": 2}`, our parser will simply evaluate `assign()` twice, leaving your variable with the last seen value.

* **Dynamic / Heap-Based Keys:** Creating keys at runtime using `std::string` formatting is intentionally unsupported. Keys must be resolvable to `const char*` or `string_view` (typically living in your `.rodata` segment) to guarantee predictable memory usage.