#pragma once

#include <cstddef>
#include <span>
#include <string_view>

namespace sjson {
class ParseNodeBase;

std::size_t decode_json_string(std::string_view input, std::span<char> out_buffer);
void parse_json_nodes(std::string_view json, std::span<ParseNodeBase*> nodes,
                      std::span<std::string_view> path_stack);

// Out-of-line coercion functions to prevent template bloat across consumer TUs
void coerce_value(std::string_view raw_val, bool is_null, bool& target);
void coerce_value(std::string_view raw_val, bool is_null, char& target);
void coerce_value(std::string_view raw_val, bool is_null, signed char& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned char& target);
void coerce_value(std::string_view raw_val, bool is_null, short& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned short& target);
void coerce_value(std::string_view raw_val, bool is_null, int& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned int& target);
void coerce_value(std::string_view raw_val, bool is_null, long& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned long& target);
void coerce_value(std::string_view raw_val, bool is_null, long long& target);
void coerce_value(std::string_view raw_val, bool is_null, unsigned long long& target);
void coerce_value(std::string_view raw_val, bool is_null, float& target);
void coerce_value(std::string_view raw_val, bool is_null, double& target);
void coerce_value(std::string_view raw_val, bool is_null, std::string_view& target);
void coerce_value(std::string_view raw_val, bool is_null, std::span<char>& target);
void coerce_value(std::string_view raw_val, bool is_null, std::string& target);

template <typename TargetT>
void coerce_value(std::string_view raw_val, bool is_null, TargetT& target) {
  static_assert(sizeof(TargetT) == 0, "StackJson: Unsupported target type bound to JSON node.");
}
}  // namespace sjson