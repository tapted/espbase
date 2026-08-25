#pragma once

#include <string_view>
#include <type_traits>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/printer.hpp"

namespace sjson {

template <typename... Nodes>
class Document;
template <typename... Elements>
class ArrayDocument;

template <typename T>
void write_json_value(Buffer& buffer, T& value) {
  using DecayedT = std::decay_t<T>;

  if constexpr (std::is_invocable_v<T, Printer&>) {
    // inline "printf" support: `node("key", [&](auto& print) { print("%s_%s", foo, bar); })`
    buffer.write("\"");
    Printer printer(buffer);
    value(printer);
    buffer.write("\"");
  } else if constexpr (std::is_convertible_v<T, std::string_view>) {
    // Strings
    buffer.write_escaped(std::string_view(value));
  } else if constexpr (std::is_same_v<DecayedT, bool>) {
    // Intercept bool before is_integral catches it
    buffer.write(value ? "true" : "false");
  } else if constexpr (std::is_same_v<DecayedT, std::nullptr_t>) {
    // Handle null values
    buffer.write("null");
  } else if constexpr (std::is_integral_v<T>) {
    // Integers
    if constexpr (std::is_signed_v<T>) {
      Printer::printx(buffer, "%lld", static_cast<long long>(value));
    } else {
      Printer::printx(buffer, "%llu", static_cast<unsigned long long>(value));
    }
  } else if constexpr (std::is_floating_point_v<T>) {
    // Floats
    Printer::printx(buffer, "%g", static_cast<double>(value));
  } else {
    // Nested Documents and ArrayDocuments
    value.emit_value(buffer);
  }
}

}  // namespace sjson