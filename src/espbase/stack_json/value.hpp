#pragma once

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "espbase/stack_json/array_emitter.hpp"
#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/printer.hpp"

namespace sjson {

template <typename... Nodes>
class Document;
template <typename... Elements>
class ArrayDocument;
template <typename Callback>
class StreamArrayDocument;

// Out-of-line scalar emitters implemented in document.cpp
void write_json_value(Buffer& buffer, long long value);
void write_json_value(Buffer& buffer, unsigned long long value);
void write_json_value(Buffer& buffer, long value);
void write_json_value(Buffer& buffer, unsigned long value);
void write_json_value(Buffer& buffer, int value);
void write_json_value(Buffer& buffer, unsigned int value);
void write_json_value(Buffer& buffer, short value);
void write_json_value(Buffer& buffer, unsigned short value);
void write_json_value(Buffer& buffer, signed char value);
void write_json_value(Buffer& buffer, unsigned char value);
void write_json_value(Buffer& buffer, double value);
void write_json_value(Buffer& buffer, float value);
void write_json_value(Buffer& buffer, bool value);
void write_json_value(Buffer& buffer, std::nullptr_t);
void write_json_value(Buffer& buffer, const char* value);
void write_json_value(Buffer& buffer, std::string_view value);
void write_json_value(Buffer& buffer, const std::string& value);

// Fallback template for lambdas (printers, array emitters) and nested documents
template <typename T>
  requires(!std::is_fundamental_v<std::decay_t<T>> && !std::is_convertible_v<T, std::string_view> &&
           !std::is_same_v<std::decay_t<T>, std::nullptr_t>)
void write_json_value(Buffer& buffer, T&& value) {
  using DecayedT = std::decay_t<T>;
  if constexpr (std::is_invocable_v<DecayedT, Printer&>) {
    // inline "printf" support: `node("key", [&](auto& print) { print("%s_%s", foo, bar); })`
    buffer.write("\"");
    Printer printer(buffer);
    value(printer);
    buffer.write("\"");
  } else if constexpr (std::is_invocable_v<DecayedT, ArrayEmitter&>) {
    // inline array streaming support: `node("key", [&](ArrayEmitter& stream) { stream.emit(42); })`
    buffer.write("[");
    ArrayEmitter emitter(buffer);
    value(emitter);
    buffer.write("]");
  } else {
    // Nested Documents and ArrayDocuments
    static_assert(
        requires { value.emit_value(buffer); },
        "StackJson Error: Unsupported type passed to node(). "
        "Values must be numbers, strings, bools, lambdas, or StackJson Documents/Arrays.");

    if constexpr (requires { value.emit_value(buffer); }) {
      value.emit_value(buffer);
    }
  }
}

template <typename T>
bool ArrayEmitter::emit(T&& value) {
  if (!before_element()) return false;
  write_json_value(buffer_, std::forward<T>(value));
  return !buffer_.bad();
}

}  // namespace sjson
