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
#include "espbase/stack_json/json.hpp"

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

### In-Place String Formatting (Printf Callback)

When generating dynamic strings like MQTT topics or unique IDs, you usually have to waste stack space on temporary buffers (`char topic_buf[128]`) and call `snprintf`.

StackJson eliminates this by accepting a lambda function. It exposes a `printf`-compatible formatter that writes *directly* into the unused tail of the `OutputBuffer`. It even applies JSON escaping in-place using a highly optimized backwards-expansion algorithm—meaning zero intermediate buffers and no `std::memmove`.

```cpp
const char* device_prefix = "espuck";
const char* sensor_type = "temperature";

auto doc = stack_json(
    node("name", "Kitchen Temp"),
    
    // Formats directly into the final JSON payload safely!
    node("state_topic", [&](auto& print) {
        print("homeassistant/sensor/%s_%s/state", device_prefix, sensor_type);
    }),

    // Fully supports automatic JSON escaping of your formatted output
    node("chaotic_text", [&](auto& print) {
        print("Line 1\nLine 2 with \"quotes\"");
        print(" print again - it will get appended");
    })
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

### Pretty Printing (Opt-In & Decoupled)

Formatting JSON with newlines and spaces is essential for debugging, but it wastes binary size if forced. StackJson handles this via the **Decorator Pattern**. You simply wrap your buffer in a `PrettyBuffer`.

Because it's completely decoupled from the generation tree, if you don't use it, the compiler's dead-code elimination (DCE) strips it out entirely!

```cpp
StackBuffer<256> raw_buffer;
PrettyBuffer pretty(raw_buffer, 2); // 2 spaces per indent

doc.emit(pretty); 
// Emits fully formatted JSON directly into raw_buffer

```

### Dynamic Buffers (Heap / PSRAM Support)

When generating massive JSON documents (like Home Assistant MQTT discovery payloads) that exceed your RTOS stack limits, you can use `DynamicBuffer`. 

`DynamicBuffer` implements the `OutputBuffer` interface but backs it with a dynamically growing standard library container. It is fully template-decoupled: it works out-of-the-box with `std::vector<char>`, but you can easily substitute a custom STL allocator to route memory to an ESP32's SPIRAM/PSRAM.

It also automatically maintains a "watermark" capacity (`ExpandSize`, defaulting to 256 bytes) at the end of the buffer to guarantee there is always contiguous space available for your zero-copy `printf` callbacks, without causing out-of-bounds errors.

```cpp

// Default routes to ESP32 PSRAM using a custom STL allocator!
DynamicBuffer<> psram_buffer;

// Or use standard heap allocation...
DynamicBuffer<std::vector<char>> heap_buffer;

auto doc = stack_json(
    node("device_class", "temperature"),
    node("state_topic", [&](auto& print) {
        // The buffer guarantees at least 256 bytes are ready for this expansion,
        // automatically resizing the underlying vector if necessary.
        print("home/sensor/%s", config.identifier); 
    })
);

doc.emit(psram_buffer);

// c_str() safely null-terminates and returns a pointer to the vector's memory
mqtt_client.publish(psram_buffer.c_str());
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

### Relaxed Parsing (JSONC, Comments & Commas)

IoT config files are often written by humans. StackJson's parser acts like a forgiving JSON5/JSONC engine out of the box. It natively ignores trailing commas and gracefully skips `//` and `/* */` comments.

```cpp
std::string_view json = R"(
{
    // Configure the main port
    "port": 8080, 
    "history": [1, 2, 3,], /* Note the trailing commas! */
}
)";

int port = 0;
auto parser = json_parser(bind("port", port));
parser.parse(json); // Succeeds effortlessly

```

### Dynamic Nodes (Variant Parsing, Sub-Parsing & Arrays)

Sometimes you don't know exactly what type a value will be, or you want to parse an array of unknown length. By calling `bind()` with only a path (no target variable), StackJson creates a `DynamicNode`. 

A `DynamicNode` captures the raw, zero-copy `std::string_view` of the matched JSON block (even full objects or arrays). You can then interrogate its state, extract its value, sub-parse it, or iterate over it.

#### 1. Variant Extraction & State Tracking
You can check if a node was present in the payload or explicitly set to `null` before deciding what to do with it.

```cpp
auto version = bind("version");
auto image = bind("image");
auto not_set = bind("not_set");

auto parser = json_parser(version, image, not_set);
parser.parse(R"({"version": 11, "image": "foo.bin"})");

if (image.is_set() && !image.is_null()) {
    std::string img;
    image >> img; // Auto-coerces the captured JSON into your variable
}

int ver;
version >> ver; // Evaluates to true and sets ver to 11

std::string missing;
not_set >> missing; // Evaluates to false; missing remains untouched

```

#### 2. Sub-Parsing (Nested Objects)

If a JSON payload contains a complex nested object, a `DynamicNode` captures the entire block `{...}`. You can then spin up a child parser directly on that node to extract its internal fields.

```cpp
auto info = bind(path("project", "info"));
auto parser = json_parser(info);

// Captures the entire "info" object block
parser.parse(R"({"project": {"info": {"name": "espuck", "tags": 42}}})");

std::string name;
int tags = 0;

// Sub-parse the captured block
info.parse(
    bind("name", name),
    bind("tags", tags)
);

```

#### 3. Array Iteration

`DynamicNode` natively supports array iteration. By using a `while(node >> target)` loop, it acts as a lightweight tokenizer, extracting one element at a time from the array.

```cpp
auto tags = bind("tags");
auto parser = json_parser(tags);
parser.parse(R"({"tags": ["wifi", "ble", "touch"]})");

std::string tag;
while (tags >> tag) {
    // Loops 3 times, yielding "wifi", "ble", and "touch"
}

```

#### 4. Object Array Iteration (Zero-Copy!)

By combining array iteration with sub-parsing, you can easily parse arrays of objects without ever allocating a vector or loading the array into memory! Just stream the array elements into a `std::string_view` (which grabs the raw `{...}` text of the element), and sub-parse it.

```cpp
auto sensors = bind("sensors");
auto parser = json_parser(sensors);
parser.parse(R"({"sensors": [{"id": "temp", "val": 22.5}, {"id": "hum", "val": 55.0}]})");

std::string_view obj_str;
while (sensors >> obj_str) {
    std::string id;
    float val = 0;
    
    // Parse the individual array element
    auto sub_parser = json_parser(bind("id", id), bind("val", val));
    sub_parser.parse(obj_str);
    
    printf("Sensor %s: %f\n", id.c_str(), val);
}

```

### Null Tracking of directly-bound nodes

Distinguishing between an omitted key and an explicit `null` (clear state) for non-dynamic bindings is also possible.

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

Depending on your memory constraints, you can choose how to handle escaped strings (like `\n`, `\"`, or `\uXXXX` sequences):

1. **`std::string_view`**: Zero-copy. You get a view directly into the raw, unparsed JSON buffer.
2. **`std::span<char>`**: Provide your own stack buffer. StackJson decodes the string (including UTF-8 translation for `\u` sequences) into your buffer and safely shrinks the span to the exact decoded length.
3. **`std::string`**: The pragmatic escape hatch. StackJson will dynamically resize the heap string to perfectly fit the decoded characters with exactly *one* allocation.

---

## 📊 Feature Support Matrix

### 🟢 Fully Supported

* **Zero-Allocation Traversal:** Generating and parsing operate completely without the heap.
* **Compile-Time Depth Calculation:** Parser bounds checking perfectly auto-sizes to the deepest path requested.
* **UTF-8 / Unicode Decoding:** Translates `\uXXXX` sequences into valid UTF-8 bytes natively, and passes raw high-byte characters (like `°C` or emojis) through safely.
* **JSONC Extensions:** Natively supports single (`//`) and multi-line (`/* */`) comments.
* **Trailing Commas:** Safely routes around trailing commas in both objects and arrays.
* **Nested Objects & Paths:** Limitless object nesting within the boundaries of your RTOS stack.
* **Auto Type Coercion:** Parser seamlessly converts strings to numbers/booleans and vice versa.

### 🟡 On the Roadmap (TODO)

* **NDJSON (Newline Delimited JSON):** Implementing a streaming reset so the parser can continuously evaluate a stream of back-to-back JSON objects over a serial or TCP socket.

### 🔴 Out of Scope / Infeasible (The Trade-offs)

Because StackJson operates entirely on the stack and refuses to allocate dynamic memory, we cannot support the following features found in heavy desktop libraries:

* **Unknown Key Iteration:** We cannot iterate over keys we don't know about. If you don't bind a `path("unknown_key")` at compile time, the parser completely ignores it.
* **Full DOM Manipulation:** We will never load an arbitrary JSON tree into a mutable in-memory DOM. You cannot parse a document, alter a specific nested node, and then re-emit it.
* **Perfect Float Round-Tripping:** To save up to 150kB of binary size (by avoiding algorithms like Ryu/Dragonbox), we fall back to `<charconv>` and `<cstdio>`. This is fast and small, but may slightly alter float string representations (e.g., `3.14` might emit as `3.1400000000000001`).
* **Duplicate Key Detection:** If a payload contains `{"a": 1, "a": 2}`, our parser will evaluate the assignment twice, leaving your variable with the last seen value.

### 🟡 Dynamic / Heap-Based Keys (Supported via LValues)

StackJson does not allocate memory for keys, but it **can** bind to your dynamically generated keys (e.g., `std::string`). Thanks to perfect forwarding, the library safely captures a reference to your local string variables. To protect you from dangling memory, a compile-time guardrail will intentionally break the build if you attempt to bind to a temporary/rvalue string.
