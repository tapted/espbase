#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace sjson {
class ParseNodeBase;

std::size_t decode_json_string(std::string_view input, std::span<char> out_buffer);
void parse_json_nodes(std::string_view json, std::span<ParseNodeBase*> nodes,
                      std::span<std::string_view> path_stack);

// Core out-of-line coercion functions to prevent template bloat across consumer TUs
void coerce_value(std::string_view raw_val, bool is_null, bool& target);
void coerce_value(std::string_view raw_val, bool is_null, int& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned int& target);
void coerce_value(std::string_view raw_val, bool is_null, long long& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned long long& target);
void coerce_value(std::string_view raw_val, bool is_null, float& target);
void coerce_value(std::string_view raw_val, bool is_null, double& target);
void coerce_value(std::string_view raw_val, bool is_null, std::string_view& target);
void coerce_value(std::string_view raw_val, bool is_null, std::span<char>& target);
void coerce_value(std::string_view raw_val, bool is_null, std::string& target);

// Inlined delegating overloads to eliminate trampoline functions in parser.o
inline void coerce_value(std::string_view raw_val, bool is_null, char& target) {
  int v = 0;
  coerce_value(raw_val, is_null, v);
  target = static_cast<char>(v);
}
inline void coerce_value(std::string_view raw_val, bool is_null, signed char& target) {
  int v = 0;
  coerce_value(raw_val, is_null, v);
  target = static_cast<signed char>(v);
}
inline void coerce_value(std::string_view raw_val, bool is_null, unsigned char& target) {
  unsigned int v = 0;
  coerce_value(raw_val, is_null, v);
  target = static_cast<unsigned char>(v);
}
inline void coerce_value(std::string_view raw_val, bool is_null, short& target) {
  int v = 0;
  coerce_value(raw_val, is_null, v);
  target = static_cast<short>(v);
}
inline void coerce_value(std::string_view raw_val, bool is_null, unsigned short& target) {
  unsigned int v = 0;
  coerce_value(raw_val, is_null, v);
  target = static_cast<unsigned short>(v);
}
inline void coerce_value(std::string_view raw_val, bool is_null, long& target) {
  if constexpr (sizeof(long) == sizeof(int)) {
    int v = 0;
    coerce_value(raw_val, is_null, v);
    target = static_cast<long>(v);
  } else {
    long long v = 0;
    coerce_value(raw_val, is_null, v);
    target = static_cast<long>(v);
  }
}
inline void coerce_value(std::string_view raw_val, bool is_null, unsigned long& target) {
  if constexpr (sizeof(unsigned long) == sizeof(unsigned int)) {
    unsigned int v = 0;
    coerce_value(raw_val, is_null, v);
    target = static_cast<unsigned long>(v);
  } else {
    unsigned long long v = 0;
    coerce_value(raw_val, is_null, v);
    target = static_cast<unsigned long>(v);
  }
}

template <typename TargetT>
void coerce_value(std::string_view raw_val, bool is_null, TargetT& target) {
  static_assert(sizeof(TargetT) == 0, "StackJson: Unsupported target type bound to JSON node.");
}
}  // namespace sjson