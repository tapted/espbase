#pragma once

#include <algorithm>
#include <charconv>
#include <span>
#include <string_view>
#include <utility>

#include "espbase/stack_json/path.hpp"

namespace sjson {
class ParseNodeBase;

std::size_t decode_json_string(std::string_view input, std::span<char> out_buffer);
void parse_json_nodes(std::string_view json, std::span<ParseNodeBase*> nodes,
                      std::span<std::string_view> path_stack);

class ParseNodeBase {
 protected:
  bool is_set_ = false;
  bool is_null_ = false;

 public:
  virtual ~ParseNodeBase() = default;

  virtual const PathBase& path() const = 0;

  // The parser will call this when it finds a matching path
  virtual void assign(std::string_view raw_val, bool is_string, bool is_null) = 0;

  bool is_set() const { return is_set_; }
  bool is_null() const { return is_null_; }
  void reset() {
    is_set_ = false;
    is_null_ = false;
  }
};

template <typename PathT, typename TargetT>
class BindNode : public ParseNodeBase {
  PathT path_;
  TargetT& target_;  // Reference to the user's variable
 public:
  static constexpr std::size_t static_depth = PathT::static_depth;

  BindNode(PathT p, TargetT& t) : path_(p), target_(t) {}

  const PathBase& path() const override { return path_; }

  void assign(std::string_view raw_val, bool is_string, bool is_null) override {
    using DecayedT = std::decay_t<TargetT>;
    is_set_ = true;
    is_null_ = is_null;

    if (is_null) {
      target_ = TargetT{};  // User request: set to 0/false/empty on null
      return;
    }

    if constexpr (std::is_same_v<DecayedT, std::string_view>) {
      // Fast path: User gets a read-only view of the raw, escaped JSON data
      target_ = raw_val;
    } else if constexpr (std::is_same_v<DecayedT, std::span<char>>) {
      // Auto-decode path: User provided a mutable fixed buffer
      std::size_t decoded_len = decode_json_string(raw_val, target_);
      target_ = target_.subspan(0, decoded_len);
    } else if constexpr (std::is_same_v<DecayedT, std::string>) {
      // Pragmatic path: Dynamic string allocation.
      // The decoded string will never be larger than the raw escaped string.
      target_.resize(raw_val.size());

      // Decode directly into the string's internal memory
      std::size_t decoded_len = decode_json_string(raw_val, std::span<char>(target_));

      // Shrink to the actual unescaped length
      target_.resize(decoded_len);
    } else if constexpr (std::is_same_v<DecayedT, bool>) {
      target_ = (raw_val == "true" || raw_val == "1");
    } else if constexpr (std::is_integral_v<TargetT> || std::is_floating_point_v<TargetT>) {
      // Auto-convert strings to numbers, or parse raw numbers
      std::from_chars(raw_val.data(), raw_val.data() + raw_val.size(), target_);
    } else {
      static_assert(false,
                    "StackJson: Unsupported target type bound to JSON node. "
                    "Must be string, string_view, span<char>, bool, or a numeric type.");
    }
  }
};

template <typename... Nodes>
class Parser {
  std::tuple<Nodes...> nodes_;

  // Compile-time magic: Calculate the exact max depth we need!
  static constexpr std::size_t MaxDepth = std::max({std::size_t{1}, Nodes::static_depth...});

 public:
  explicit Parser(Nodes... nodes) : nodes_(std::move(nodes)...) {}

  void parse(std::string_view json) {
    std::array<ParseNodeBase*, sizeof...(Nodes)> node_ptrs =
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          return std::array<ParseNodeBase*, sizeof...(Nodes)>{&std::get<I>(nodes_)...};
        }(std::make_index_sequence<sizeof...(Nodes)>{});

    // The trampoline stack allocation is now perfectly auto-sized
    std::array<std::string_view, MaxDepth> path_stack;

    for (auto* n : node_ptrs) n->reset();

    parse_json_nodes(json, node_ptrs, path_stack);
  }

  template <std::size_t Index>
  bool was_set() const {
    return std::get<Index>(nodes_).is_set();
  }
  template <std::size_t Index>
  bool was_null() const {
    return std::get<Index>(nodes_).is_null();
  }
};

template <typename... Nodes>
auto json_parser(Nodes... nodes) {
  return Parser<Nodes...>(std::move(nodes)...);
}

// The builder function for the schema
template <typename PathT, typename TargetT>
auto bind(PathT p, TargetT& target) {
  return BindNode<PathT, TargetT>(p, target);
}

// Overload for root keys
template <typename TargetT>
auto bind(const char* root_key, TargetT& target) {
  return bind(path(root_key), target);
}

}  // namespace sjson