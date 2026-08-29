#pragma once

#include <charconv>
#include <cstddef>
#include <span>
#include <string_view>

namespace sjson {
class ParseNodeBase;

std::size_t decode_json_string(std::string_view input, std::span<char> out_buffer);
void parse_json_nodes(std::string_view json, std::span<ParseNodeBase*> nodes,
                      std::span<std::string_view> path_stack);

// Centralized coercion logic so both BindNode and DynamicNode can share it
template <typename TargetT>
void coerce_value(std::string_view raw_val, bool is_null, TargetT& target) {
  using DecayedT = std::decay_t<TargetT>;

  if (is_null) {
    target = TargetT{};
    return;
  }

  if constexpr (std::is_same_v<DecayedT, std::string_view>) {
    target = raw_val;
  } else if constexpr (std::is_same_v<DecayedT, std::span<char>>) {
    std::size_t decoded_len = decode_json_string(raw_val, target);
    target = target.subspan(0, decoded_len);
  } else if constexpr (std::is_same_v<DecayedT, std::string>) {
    target.resize(raw_val.size());
    std::size_t decoded_len = decode_json_string(raw_val, std::span<char>(target));
    target.resize(decoded_len);
  } else if constexpr (std::is_same_v<DecayedT, bool>) {
    target = (raw_val == "true" || raw_val == "1");
  } else if constexpr (std::is_integral_v<DecayedT> || std::is_floating_point_v<DecayedT>) {
    std::from_chars(raw_val.data(), raw_val.data() + raw_val.size(), target);
  } else {
    static_assert(false, "StackJson: Unsupported target type bound to JSON node.");
  }
}
}  // namespace sjson