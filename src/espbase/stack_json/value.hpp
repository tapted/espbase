#pragma once

#include <cstdio>
#include <string_view>
#include <type_traits>

#include "espbase/stack_json/buffer.hpp"

namespace sjson {

template <typename... Nodes>
class Document;
template <typename... Elements>
class ArrayDocument;

template <typename T>
void write_json_value(Buffer& buffer, T& value) {
  if constexpr (std::is_convertible_v<T, std::string_view>) {
    // Strings
    buffer.write_quoted(std::string_view(value));
  } else if constexpr (std::is_integral_v<T>) {
    // Integers
    char buf[32];
    int len;
    if constexpr (std::is_signed_v<T>) {
      len = snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
    } else {
      len = snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
    }
    if (len > 0) buffer.write(std::string_view(buf, len));
  } else if constexpr (std::is_floating_point_v<T>) {
    // Floats
    char buf[32];
    // %g is ideal for JSON: omits trailing zeros and uses scientific notation only when necessary
    int len = snprintf(buf, sizeof(buf), "%g", static_cast<double>(value));
    if (len > 0) buffer.write(std::string_view(buf, len));
  } else {
    // Nested Documents and ArrayDocuments
    value.emit_value(buffer);
  }
}

}  // namespace sjson