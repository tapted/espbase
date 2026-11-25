#pragma once

/**
 * @file parse.hpp
 * @brief Public umbrella header for the StackJson zero-allocation JSON parsing API.
 *
 * @details
 * Include this single header to access the full StackJson parsing surface:
 * - Compile-time path construction (`sjson::path()`)
 * - Typed static bindings (`sjson::bind(path, target)`)
 * - Dynamic/variant bindings (`sjson::bind(path)` → `DynamicNode`)
 * - Catch-all unknown-key callback (`sjson::bind_unknown(callback)`)
 * - Parser factory and execution (`sjson::json_parser(nodes...).parse(json)`)
 *
 * StackJson's parser is a **zero-allocation, single-pass SAX engine**.  It
 * never builds a DOM; instead it matches JSON keys against a compile-time graph
 * of `ParseNode` objects rooted in your local variables.  Maximum stack-frame
 * depth is computed at compile time from the deepest `path()` requested.
 *
 * ---
 *
 * ## 1. `sjson::bind(path, target)` — static typed binding
 *
 * Associates a JSON path with a local variable.  The parser deposits the
 * matched value directly into `target`, auto-coercing the raw JSON token into
 * the variable's C++ type.
 *
 * ### Supported target types
 *
 * | C++ type            | Behaviour                                                   |
 * |---------------------|-------------------------------------------------------------|
 * | `bool`              | Accepts `true`/`false` literals and string coercions.       |
 * | `int`, `uint8_t`, `int32_t`, … | Any standard integer width; string tokens like `"50"` are
 * auto-converted. | | `float`, `double`   | Parsed with `<charconv>` / `<cstdio>` fast path. | |
 * `std::string_view`  | **Zero-copy** — points into the original JSON buffer.  Raw escaped bytes
 * are preserved (backslashes remain). | | `std::span<char>`   | **Stack-decoded** — supply your own
 * char array; StackJson decodes escape sequences (including `\uXXXX` → UTF-8) into it and **shrinks
 * the span** to the exact decoded length. | | `std::string`       | **Heap-decoded** — StackJson
 * resizes the string to fit with exactly one allocation, decoding all escape sequences. |
 *
 * @code{.cpp}
 * // ── bool ────────────────────────────────────────────────────────────────
 * bool active = false;
 * auto p = sjson::json_parser(sjson::bind("active", active));
 * p.parse(R"({"active": true})");
 * // active == true
 *
 * // ── int (auto-converts the string "50" to integer 50) ───────────────────
 * int brightness = 0;
 * auto p2 = sjson::json_parser(sjson::bind("brightness", brightness));
 * p2.parse(R"({"brightness": "50"})");
 * // brightness == 50
 *
 * // ── float / double ──────────────────────────────────────────────────────
 * float temp = 0.f;
 * sjson::json_parser(sjson::bind("temp", temp)).parse(R"({"temp": 22.5})");
 * // temp == 22.5f
 *
 * // ── std::string_view (zero-copy, raw escaped bytes) ─────────────────────
 * std::string_view state;
 * sjson::json_parser(sjson::bind("state", state)).parse(R"({"state": "ON"})");
 * // state points into the original JSON; backslash sequences are NOT decoded.
 *
 * // ── std::span<char> (stack-decoded, shrinks to exact length) ─────────────
 * char buf[64];
 * std::span<char> sp(buf);
 * sjson::json_parser(sjson::bind("text", sp))
 *     .parse(R"({"text": "Hello\nWorld"})");
 * // sp.size() == 11, sp data == "Hello\nWorld" (decoded)
 *
 * // ── std::string (heap-decoded, one allocation) ───────────────────────────
 * std::string desc;
 * sjson::json_parser(sjson::bind("desc", desc))
 *     .parse(R"({"desc": "Board with\ttabs and \"quotes\""})");
 * // desc == "Board with\ttabs and \"quotes\""
 * @endcode
 *
 * ### Nested paths
 *
 * @code{.cpp}
 * uint8_t r = 0, g = 0, b = 0;
 * auto color = sjson::path("color");
 *
 * auto parser = sjson::json_parser(
 *     sjson::bind(color("r"), r),
 *     sjson::bind(color("g"), g),
 *     sjson::bind(color("b"), b)
 * );
 * parser.parse(R"({"color": {"r": 100, "g": 30, "b": 10}})");
 * @endcode
 *
 * ### Root-key shorthand
 *
 * When the path has only one element you can pass a bare `const char*` root key
 * instead of constructing a `path()`:
 *
 * @code{.cpp}
 * // These two are equivalent:
 * sjson::bind(sjson::path("brightness"), brightness);
 * sjson::bind("brightness",             brightness);   // ← shorthand
 * @endcode
 *
 * ---
 *
 * ## 2. `sjson::bind(path)` / `sjson::bind(root_key)` — dynamic binding
 *
 * When you omit the target variable, `bind()` returns a `DynamicNode`.  The
 * node captures the **raw JSON block** (including `{...}`, `[...]`, or a bare
 * token) for the matched path.  You can interrogate it, extract from it, or
 * sub-parse it after the top-level parse completes.
 *
 * @code{.cpp}
 * auto version = sjson::bind("version");   // DynamicNode
 * auto image   = sjson::bind("image");     // DynamicNode
 *
 * sjson::json_parser(version, image)
 *     .parse(R"({"version": 11, "image": "foo.bin"})");
 *
 * if (image.is_set() && !image.is_null()) {
 *     std::string img = image.as<std::string>();  // "foo.bin"
 * }
 *
 * int ver = 0;
 * version >> ver;   // true; ver == 11
 * @endcode
 *
 * ---
 *
 * ## 3. `sjson::DynamicNodeBase` — inspecting a captured value
 *
 * `DynamicNodeBase` is the base class of every node returned by the two-argument
 * `bind()` overload and of the node passed to `bind_unknown` callbacks.
 *
 * ### State tracking
 *
 * | Method        | Description                                                            |
 * |---------------|------------------------------------------------------------------------|
 * | `is_set()`    | `true` if the JSON payload contained the bound path (even if `null`).  |
 * | `is_null()`   | `true` if the value was the JSON literal `null`.                       |
 *
 * ### Type identification
 *
 * | Method         | Returns `true` when…                                                  |
 * |----------------|-----------------------------------------------------------------------|
 * | `is_string()`  | The matched token was a JSON string (quoted).                         |
 * | `is_array()`   | The matched block starts with `[`.                                    |
 * | `is_object()`  | The matched block starts with `{`.                                    |
 * | `is_boolean()` | The raw token is exactly `true` or `false`.                           |
 * | `is_number()`  | The token is set, non-null, non-string, non-array, non-object, non-boolean. |
 *
 * ### `raw()` — zero-copy view of the captured block
 *
 * Returns a `std::string_view` pointing into the original JSON buffer.  For
 * arrays and objects the entire bracketed block is included.
 *
 * @code{.cpp}
 * auto cfg = sjson::bind("config");
 * sjson::json_parser(cfg)
 *     .parse(R"({"config": {"speed": 100, "mode": "auto"}})");
 *
 * // cfg.raw() == R"({"speed": 100, "mode": "auto"})"
 * @endcode
 *
 * ### `operator>>(target)` — extract / coerce
 *
 * Coerces the captured value into `target` using the same rules as static
 * bindings.  Returns `true` on success, `false` if the node was never set.
 *
 * @code{.cpp}
 * int ver = 0;
 * if (version >> ver) {
 *     // ver has been populated
 * }
 * @endcode
 *
 * When the node holds a **JSON array**, `operator>>` acts as an iterator:
 * each call extracts the next element.  Returns `false` when the array is
 * exhausted (see §9 for the full array pattern).
 *
 * ### `as<T>()` — shorthand extract-and-return
 *
 * Calls `operator>>` and returns the result by value.  Defaults to
 * `std::string` when no template argument is supplied.
 *
 * @code{.cpp}
 * // These are equivalent:
 * std::string s = node.as<std::string>();
 * std::string s2; node >> s2;
 *
 * int n = node.as<int>();
 * @endcode
 *
 * ### `parse(bind(...), ...)` — sub-parse a captured object block
 *
 * Spins up a child parser over the node's captured raw block.  Useful for
 * lazily parsing nested objects after the top-level scan.
 *
 * @code{.cpp}
 * auto info = sjson::bind(sjson::path("project", "info"));
 * sjson::json_parser(info)
 *     .parse(R"({"project": {"info": {"name": "espuck", "tags": 42}}})");
 *
 * std::string name;
 * int tags = 0;
 * info.parse(sjson::bind("name", name), sjson::bind("tags", tags));
 * // name == "espuck", tags == 42
 * @endcode
 *
 * ---
 *
 * ## 4. `sjson::bind_unknown(callback)` — catch-all for unmapped paths
 *
 * Returns a special node that fires `callback` for every JSON key that is
 * **not** matched by any other binding in the same parser.
 *
 * Callback signature:
 * @code{.cpp}
 * void callback(const sjson::PathBase& path, sjson::DynamicNodeBase& node);
 * @endcode
 *
 * @code{.cpp}
 * int known_val = 0;
 *
 * auto parser = sjson::json_parser(
 *     sjson::bind_unknown([](const sjson::PathBase& path, sjson::DynamicNodeBase& node) {
 *         std::string_view key = path.get_element(path.depth() - 1);
 *         if (node.is_array()) {
 *             int v;
 *             while (node >> v) {  consume array elements  }
 *         } else {
 *             // node.raw() gives the raw JSON token or block
 *         }
 *     }),
 *     sjson::bind("known_key", known_val)
 * );
 * parser.parse(json);
 * @endcode
 *
 * ---
 *
 * ## 5. `sjson::json_parser(nodes...)` — parser factory
 *
 * Returns a `Parser<Nodes...>` object.  Call `.parse(json_string_view)` to
 * execute the scan.  You can call `.parse()` multiple times on the same parser
 * to process successive payloads (node state resets before each pass).
 *
 * ### `parser.was_set<I>()`
 * Returns `true` if the node at zero-based index `I` was present in the last
 * parsed payload (even if its value was `null`).
 *
 * ### `parser.was_null<I>()`
 * Returns `true` if the node at index `I` was present **and** its value was
 * the JSON literal `null`.
 *
 * @code{.cpp}
 * std::string_view state;
 * int brightness = 0;
 *
 * auto parser = sjson::json_parser(
 *     sjson::bind("state",      state),       // index 0
 *     sjson::bind("brightness", brightness)   // index 1
 * );
 * parser.parse(R"({"state": "ON", "brightness": "50"})");
 *
 * // state == "ON", brightness == 50
 * parser.template was_set<0>();   // true  — "state" was present
 * parser.template was_null<1>();  // false — "brightness" was not null
 * @endcode
 *
 * ---
 *
 * ## 6. `sjson::json_parser(callback, nodes...)` — catch-all lambda overload
 *
 * As a convenience shorthand, if the **first** argument to `json_parser` is not
 * a `ParseNodeBase` subclass, it is treated as an unknown-key callback (exactly
 * as if it had been wrapped in `bind_unknown()`).  Explicit bindings follow
 * normally as remaining arguments.
 *
 * @code{.cpp}
 * auto parser = sjson::json_parser(
 *     // 1. The catch-all lambda (must come first):
 *     [&](const sjson::PathBase& path, sjson::DynamicNodeBase& node) {
 *         std::string_view key = path.get_element(path.depth() - 1);
 *         if      (node.is_string())  {  ...  }
 *         else if (node.is_number())  {  ...  }
 *         else if (node.is_object())  {  ...  }
 *     },
 *     // 2. Static bindings follow:
 *     sjson::bind("known_key", known_val)
 * );
 * parser.parse(json);
 * @endcode
 *
 * ---
 *
 * ## 7. Null tracking on direct bindings
 *
 * To distinguish an **omitted** key from a key explicitly set to `null`, keep a
 * reference to the `BindNode` returned by `bind()` and query it after parsing.
 * The same information is also accessible via `parser.was_set<I>()` /
 * `parser.was_null<I>()`.
 *
 * @code{.cpp}
 * int brightness = 100;
 * auto brightness_node = sjson::bind("brightness", brightness);
 * auto parser = sjson::json_parser(brightness_node);   // index 0
 *
 * parser.parse(R"({"brightness": null})");
 *
 * // brightness_node.is_set()  == true   (key was present)
 * // brightness_node.is_null() == true   (value was null → target reset to 0)
 * // brightness                == 0
 *
 * parser.parse(R"({"other": "value"})");
 *
 * // brightness_node.is_set()  == false  (key was absent)
 * // brightness                == 0      (unchanged from prior parse)
 * @endcode
 *
 * ---
 *
 * ## 8. JSONC support (comments & trailing commas)
 *
 * StackJson's parser silently accepts the following extensions without any
 * configuration flags:
 *
 * - **Line comments** (`//` to end of line)
 * - **Block comments** (` … `)
 * - **Trailing commas** in both objects and arrays
 *
 * @code{.cpp}
 * std::string_view json = R"(
 * {
 *     // The main configuration for the device
 *     "hostname": "espuck_01",   // Do not change this over OTA
 *     "port": 8080,              // Webserver port (trailing comma below is OK)
 *     "history": [1, 2, 3,],
 * }
 * )";
 *
 * std::string_view hostname;
 * int port = 0;
 * sjson::json_parser(sjson::bind("hostname", hostname),
 *                    sjson::bind("port",     port)).parse(json);
 * // hostname == "espuck_01", port == 8080
 * @endcode
 *
 * ---
 *
 * ## 9. Array iteration pattern
 *
 * A `DynamicNode` over a JSON array acts as a lightweight forward iterator.
 * Each `operator>>` call extracts the next element and advances an internal
 * cursor; the loop terminates when it returns `false`.
 *
 * @code{.cpp}
 * auto tags = sjson::bind("tags");
 * sjson::json_parser(tags)
 *     .parse(R"({"tags": ["wifi", "ble", "touch"]})");
 *
 * std::string tag;
 * while (tags >> tag) {
 *     // Loops 3 times, yielding "wifi", "ble", "touch"
 * }
 *
 * // Integer arrays work identically:
 * auto numbers = sjson::bind("numbers");
 * sjson::json_parser(numbers)
 *     .parse(R"({"numbers": [10, 20, 30]})");
 *
 * int n;
 * while (numbers >> n) { n = 10, 20, 30 in turn }
 * @endcode
 *
 * ---
 *
 * ## 10. Object-array pattern (zero-copy streaming)
 *
 * Arrays of nested objects can be processed without allocating a container.
 * Extract each element as a `std::string_view` (capturing the raw `{...}` text)
 * and feed it into a child parser.  Only one object is materialised at a time.
 *
 * @code{.cpp}
 * auto sensors = sjson::bind("sensors");
 * sjson::json_parser(sensors).parse(R"({
 *     "sensors": [
 *         {"id": "temp", "val": 22.5},
 *         {"id": "hum",  "val": 55.0}
 *     ]
 * })");
 *
 * std::string_view obj_str;
 * while (sensors >> obj_str) {           // obj_str = raw "{...}" of one element
 *     std::string id;
 *     float val = 0.f;
 *     auto sub = sjson::json_parser(sjson::bind("id", id),
 *                                   sjson::bind("val", val));
 *     sub.parse(obj_str);
 *
 *     printf("Sensor %s: %.1f\n", id.c_str(), val);
 * }
 * // Output:
 * //   Sensor temp: 22.5
 * //   Sensor hum: 55.0
 * @endcode
 *
 * @note Zero heap allocation throughout — `sensors`, `obj_str`, `id`, and `val`
 *       all live on the call stack.
 */
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "espbase/stack_json/detail/parse_node_base.hpp"
#include "espbase/stack_json/detail/parser_impl.hpp"
#include "espbase/stack_json/path.hpp"

namespace sjson {

/**
 * @brief Result of a path-only `bind()` call or catch-all callback argument.
 *
 * @details Captures a zero-copy `std::string_view` over the raw JSON value
 * matched at runtime (including full `{...}` objects and `[...]` arrays) and
 * exposes type identification, value coercion (`>>`, `as<T>()`), array
 * iteration, and nested sub-parsing (`parse()`).
 */
class DynamicNodeBase : public ParseNodeBase {
 protected:
  std::string_view raw_val_;
  bool is_string_ = false;
  std::string_view iter_state_;
  bool iter_started_ = false;

 public:
  explicit constexpr DynamicNodeBase(bool catch_all = false) : ParseNodeBase(catch_all) {}

  /// @brief Internal parser hook called when the bound path matches.
  void assign(const PathBase&, std::string_view raw_val, bool is_string, bool is_null) override;

  /**
   * @brief Returns a zero-copy view of the raw unparsed JSON token or block.
   *
   * @details For objects and arrays this includes the outer `{...}` or `[...]`
   * delimiters. For strings, it excludes the outer quotes and preserves escapes.
   */
  std::string_view raw() const { return raw_val_; }

  /// @brief Returns `true` if the matched JSON value was a string.
  bool is_string() const { return is_string_; }
  /// @brief Returns `true` if the matched JSON value is an array (`[...]`).
  bool is_array() const { return !raw_val_.empty() && raw_val_.front() == '['; }
  /// @brief Returns `true` if the matched JSON value is an object (`{...}`).
  bool is_object() const { return !raw_val_.empty() && raw_val_.front() == '{'; }
  /// @brief Returns `true` if the matched JSON value is `true` or `false`.
  bool is_boolean() const { return raw_val_ == "true" || raw_val_ == "false"; }
  /// @brief Returns `true` if the matched JSON value is a numeric literal.
  bool is_number() const {
    return is_set_ && !is_null_ && !is_string_ && !is_array() && !is_object() && !is_boolean();
  }

  /**
   * @brief Spins up a child parser over the raw JSON block captured by this node.
   *
   * @tparam Nodes Bindings created via `sjson::bind()`.
   * @param nodes Bindings to extract from the captured sub-object.
   * @return `true` if the node was set and non-null (and thus parsed), `false` otherwise.
   */
  template <typename... Nodes>
  bool parse(Nodes&&... nodes) const;

  /**
   * @brief Extracts and coerces the value into @p target, or iterates if this node is an array.
   *
   * @details When the captured value is a JSON array, successive calls advance an
   * internal cursor and yield one element per call until the array is exhausted.
   *
   * @tparam TargetT Any supported target type (`bool`, arithmetic, `std::string_view`,
   *                 `std::span<char>`, `std::string`).
   * @param target Destination variable (left untouched if `false` is returned).
   * @return `true` if a value/element was extracted, `false` if unset or array exhausted.
   */
  template <typename TargetT>
  bool operator>>(TargetT& target);

  /**
   * @brief Shorthand helper that coerces and returns the value as @p T.
   *
   * @tparam T Destination type (defaults to `std::string`).
   * @return Extracted value, or default-constructed `T{}` if unset.
   */
  template <typename T = std::string>
  T as() {
    T val{};
    *this >> val;
    return val;
  }

 private:
  std::string_view next_array_token(bool& is_str);
  bool next_value(std::string_view& val_to_parse, bool& val_is_null);
};

}  // namespace sjson

#include "espbase/stack_json/detail/parse_node_impl.hpp"

namespace sjson {

/**
 * @brief Binds a compile-time JSON path to a typed target variable.
 *
 * @tparam PathT A `StaticPath<Depth>` created by `sjson::path()`.
 * @tparam TargetT Any supported target type (`bool`, integral, `float`, `double`,
 *                 `std::string_view`, `std::span<char>`, `std::string`).
 * @param p Path to match.
 * @param target Variable populated during `Parser::parse()`.
 */
template <typename PathT, typename TargetT>
auto bind(PathT&& p, TargetT& target) {
  return BindNode<std::decay_t<PathT>, TargetT>(std::forward<PathT>(p), target);
}

/**
 * @brief Root-key shorthand for `bind(path(root_key), target)`.
 */
template <typename TargetT>
auto bind(const char* root_key, TargetT& target) {
  return bind(path(root_key), target);
}

/**
 * @brief Creates a dynamic binding for @p p without a fixed target variable.
 *
 * @tparam PathT A `StaticPath<Depth>` created by `sjson::path()`.
 * @param p Path to match.
 * @return A `DynamicNode` (subclass of `DynamicNodeBase`) to inspect after parsing.
 */
template <typename PathT>
auto bind(PathT&& p) {
  return DynamicNode<std::decay_t<PathT>>(std::forward<PathT>(p));
}

/**
 * @brief Root-key shorthand for dynamic `bind(path(root_key))`.
 */
inline auto bind(const char* root_key) {
  return bind(path(root_key));
}

/**
 * @brief Wraps a callback as a catch-all binding for any unmatched JSON paths.
 *
 * @tparam Callback Callable with signature `void(const PathBase&, DynamicNodeBase&)`.
 * @param cb Callback invoked for each unmatched leaf/array/object path.
 */
template <typename Callback>
auto bind_unknown(Callback&& cb) {
  return UnknownBindNode<std::decay_t<Callback>>(std::forward<Callback>(cb));
}

/**
 * @brief Constructs a zero-allocation SAX parser from a pack of `bind()` nodes.
 *
 * @tparam Nodes Binding node types created by `bind()` or `bind_unknown()`.
 * @param nodes Bindings to populate when `parse(json)` is called.
 */
template <typename... Nodes>
auto json_parser(Nodes&&... nodes) {
  return Parser<Nodes...>(std::forward<Nodes>(nodes)...);
}

/**
 * @brief Overload accepting a catch-all callback as the first argument, followed by explicit bindings.
 *
 * @tparam First Callable with signature `void(const PathBase&, DynamicNodeBase&)`.
 * @tparam Rest Explicit `bind()` nodes.
 */
template <typename First, typename... Rest,
          typename = std::enable_if_t<!std::is_base_of_v<ParseNodeBase, std::decay_t<First>>>>
auto json_parser(First&& cb, Rest&&... rest) {
  return Parser<UnknownBindNode<std::decay_t<First>>, std::decay_t<Rest>...>(
      bind_unknown(std::forward<First>(cb)), std::forward<Rest>(rest)...);
}

template <typename... Nodes>
bool DynamicNodeBase::parse(Nodes&&... nodes) const {
  if (!is_set_ || is_null_) return false;
  auto p = Parser<std::decay_t<Nodes>...>(std::forward<Nodes>(nodes)...);
  p.parse(raw_val_);
  return true;
}

template <typename TargetT>
inline bool DynamicNodeBase::operator>>(TargetT& target) {
  std::string_view val_to_parse;
  bool val_is_null = false;
  if (!next_value(val_to_parse, val_is_null)) return false;
  coerce_value(val_to_parse, val_is_null, target);
  return true;
}

}  // namespace sjson
