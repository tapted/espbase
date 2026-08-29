## Migrating from ArduinoJson to StackJson

If you are coming from ArduinoJson, you are used to the Document Object Model (DOM) paradigm: allocating a memory pool, building a tree of variants in memory, and then serializing it (or vice versa). StackJson throws out the DOM entirely in favor of a strictly stack-based, zero-copy streaming architecture.

### Why You'd Want To

*   **Zero Dynamic Allocation:** No more guessing `StaticJsonDocument<N>` sizes or worrying about heap fragmentation on your ESP32.
*   **Compile-Time Safety:** C++20 concepts and fold expressions catch type errors and missing bindings at compile time, instead of failing silently at runtime.
*   **Deterministic Memory:** Peak RAM usage is exactly the size of your variables and your output buffer. You never hold a duplicate DOM representation in memory.
*   **Built for IoT:** Perfectly suited for MQTT payloads (like Home Assistant discovery and state topics) where you generate complex JSON on the fly or extract specific keys from massive incoming payloads.

### The Migration Cheatsheet

<table>
  <thead>
    <tr>
      <th>Task</th>
      <th>ArduinoJson (DOM)</th>
      <th>StackJson (Stream / SAX)</th>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td><strong>Simple Object</strong></td>
      <td>
<pre lang="cpp">StaticJsonDocument&lt;128&gt; doc;
doc["state"] = "ON";
doc["brightness"] = 255;
serializeJson(doc, buf);</pre>
      </td>
      <td>
<pre lang="cpp">auto doc = stack_json(
  node("state", "ON"),
  node("brightness", 255)
);
doc.emit(buf);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Nested Objects</strong></td>
      <td>
<pre lang="cpp">doc["color"]["r"] = 255;
doc["color"]["g"] = 128;</pre>
      </td>
      <td>
<pre lang="cpp">stack_json(
  node(path("color", "r"), 255),
  node(path("color", "g"), 128)
);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Arrays of Primitives</strong></td>
      <td>
<pre lang="cpp">JsonArray arr = doc.createNestedArray("id");
arr.add("screenie");
arr.add(54);</pre>
      </td>
      <td>
<pre lang="cpp">stack_json(
  node("id", stack_array("screenie", 54, ...))
);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Dynamic Arrays</strong></td>
      <td>
<pre lang="cpp">std::vector&lt;int&gt; arr = {42, 54};
JsonArray arr = doc.createNestedArray("id");
for (auto& i: arr) arr.add(i);</pre>
      </td>
      <td>
<pre lang="cpp">std::vector&lt;int&gt; arr = {42, 54};
stack_json(node("id", span_array(arr)));</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Arrays of Objects</strong></td>
      <td>
<pre lang="cpp">JsonArray arr = doc.createNestedArray("sensors");
JsonObject obj = arr.createNestedObject();
obj["id"] = 1;</pre>
      </td>
      <td>
<pre lang="cpp">stack_json(
  node("sensors", stack_array(
    stack_json(node("id", 1))
  ))
);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Conditional Keys</strong><br><em>(Omit if false)</em></td>
      <td>
<pre lang="cpp">if (has_color) {
  doc["color_mode"] = "rgb";
}</pre>
      </td>
      <td>
<pre lang="cpp">stack_json(
  node_if(has_color, "color_mode", "rgb")
);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Safe Null Pointers</strong></td>
      <td>
<pre lang="cpp">const char* str = nullptr;
doc["val"] = str; // Emits null</pre>
      </td>
      <td>
<pre lang="cpp">const char* str = nullptr;
stack_json(node("val", str));</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Basic Parsing</strong></td>
      <td>
<pre lang="cpp">deserializeJson(doc, payload);
int b = doc["brightness"];</pre>
      </td>
      <td>
<pre lang="cpp">int b = 0;
json_parser(
  bind("brightness", b)
).parse(payload);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Nested Parsing</strong></td>
      <td>
<pre lang="cpp">deserializeJson(doc, payload);
int r = doc["color"]["r"];</pre>
      </td>
      <td>
<pre lang="cpp">int r = 0;
json_parser(
  bind(path("color", "r"), r)
).parse(payload);</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Iterating an Array</strong><br><em>(Explicit Binding)</em></td>
      <td>
<pre lang="cpp">deserializeJson(doc, payload);
for (int v : doc["data"].as&lt;JsonArray&gt;()) {
  Serial.println(v);
}</pre>
      </td>
      <td>
<pre lang="cpp">auto arr_node = bind("data");
json_parser(arr_node).parse(payload);
int v;
while (arr_node &gt;&gt; v) {
  Serial.println(v);
}</pre>
      </td>
    </tr>
    <tr>
      <td><strong>Checking Null / Types</strong></td>
      <td>
<pre lang="cpp">deserializeJson(doc, payload);
if (doc["val"].isNull()) { ... }
if (doc["val"].is&lt;int&gt;()) { ... }</pre>
      </td>
      <td>
<pre lang="cpp">auto val = bind("val");
json_parser(val).parse(payload);
if (val.is_null()) { ... }
if (val.is_number()) { ... }</pre>
      </td>
    </tr>
  </tbody>
</table>

### Advanced: Dynamic Catch-All Parsing

ArduinoJson allows you to arbitrarily loop over documents using `for (JsonPair p : doc.as<JsonObject>())`. 

Because StackJson does not build a DOM tree, you cannot natively ask a document for its keys. However, you can achieve the exact same flexibility by passing a **callback lambda** as the first argument to `json_parser`. This acts as a "catch-all" router for any keys you didn't explicitly bind.

**Example: Catching Unknown Keys and Arrays**
```cpp
auto parser = json_parser(
    // The Catch-All Callback
    [&](const PathBase& path, DynamicNodeBase& node) {
        std::string_view key = path.get_element(path.depth() - 1);

        if (node.is_array()) {
            int item;
            while (node >> item) {
                printf("Array %.*s item: %d\n", key.size(), key.data(), item);
            }
        } else {
            // Extract the raw JSON substring exactly as captured
            std::string_view raw = node.raw();
            printf("Found unknown key %.*s: %.*s\n", key.size(), key.data(), raw.size(), raw.data());
        }
    },
    // Explicit bindings continue to work alongside the callback
    bind("known_key", known_val)
);

parser.parse(payload);

```

---

### Unsupported (Out of Scope)

Because StackJson operates strictly as a one-way streaming emitter and a SAX-style sequential parser, it lacks an in-memory document model entirely. The following ArduinoJson patterns are fundamentally incompatible with StackJson's zero-copy architecture:

* **In-Memory Mutation:** You cannot do `doc["brightness"] = 100;` to update an existing JSON payload. You must emit a fresh payload using your updated C++ variables.
* **Key Deletion:** There is no `doc.remove("key")`. If you want a key gone, use `node_if()` so it is never emitted into the buffer in the first place.
* **Deep Copying JSON:** You cannot clone a JSON object natively. If you need to manipulate a parsed payload and send it back out, you would use the parser to bind values to native C++ structs, modify the structs, and then serialize them back out using `stack_json()`.
* **Sorting Keys:** StackJson emits keys in the exact order you pass them into the `stack_json()` parameter pack.
