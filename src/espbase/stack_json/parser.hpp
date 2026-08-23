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

template <typename PathT, typename TargetT>
class BindNode : public ParseNodeBase {
  PathT path_;
  TargetT& target_;  // Reference to the user's variable on the stack.
 public:
  static constexpr std::size_t static_depth = PathT::static_depth;

  BindNode(PathT p, TargetT& t) : path_(p), target_(t) {}

  const PathBase& path() const override { return path_; }

  void assign(std::string_view raw_val, bool is_string, bool is_null) override {
    is_set_ = true;
    is_null_ = is_null;
    coerce_value(raw_val, is_null, target_);
  }
};

// Hoisted base for DynamicNode to avoid template bloat.
class DynamicNodeBase : public ParseNodeBase {
 protected:
  std::string_view raw_val_;
  bool is_string_ = false;
  std::string_view iter_state_;
  bool iter_started_ = false;

 public:
  void assign(std::string_view raw_val, bool is_string, bool is_null) override {
    is_set_ = true;
    is_null_ = is_null;
    raw_val_ = raw_val;
    is_string_ = is_string;
    iter_state_ = raw_val_;
    iter_started_ = false;
  }

  std::string_view next_array_token(bool& is_str);
};

template <typename PathT>
class DynamicNode : public DynamicNodeBase {
  PathT path_;

 public:
  static constexpr std::size_t static_depth = PathT::static_depth;

  explicit DynamicNode(PathT p) : path_(p) {}
  const PathBase& path() const override { return path_; }

  template <typename... Nodes>
  bool parse(Nodes&&... nodes) const;

  template <typename TargetT>
  bool operator>>(TargetT& target) {
    if (!is_set_) return false;

    std::string_view val_to_parse = raw_val_;
    bool val_is_null = is_null_;

    if (!raw_val_.empty() && raw_val_.front() == '[') {
      if (!iter_started_) {
        iter_state_ = raw_val_.substr(1);
        iter_started_ = true;
      }
      bool is_str_token = false;
      val_to_parse = next_array_token(is_str_token);
      if (val_to_parse.empty()) return false;
      val_is_null = (val_to_parse == "null");
    } else {
      if (iter_started_) return false;
      iter_started_ = true;
    }

    coerce_value(val_to_parse, val_is_null, target);
    return true;
  }
};

template <typename... Nodes>
class Parser {
  std::tuple<Nodes...> nodes_;

  // Use remove_reference_t so we can extract static_depth whether it's a value or a reference
  static constexpr std::size_t MaxDepth =
      std::max({std::size_t{1}, std::remove_reference_t<Nodes>::static_depth...});

 public:
  explicit Parser(Nodes&&... nodes) : nodes_(std::forward<Nodes>(nodes)...) {}

  void parse(std::string_view json) {
    std::array<ParseNodeBase*, sizeof...(Nodes)> node_ptrs =
        [&]<std::size_t... I>(std::index_sequence<I...>) {
          // If the tuple holds a reference, std::get returns it, and '&' extracts
          // the address of your original local variable.
          return std::array<ParseNodeBase*, sizeof...(Nodes)>{&std::get<I>(nodes_)...};
        }(std::make_index_sequence<sizeof...(Nodes)>{});

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

template <typename PathT>
template <typename... Nodes>
bool DynamicNode<PathT>::parse(Nodes&&... nodes) const {
  if (!is_set_ || is_null_) return false;
  auto p = Parser<std::decay_t<Nodes>...>(std::forward<Nodes>(nodes)...);
  p.parse(raw_val_);
  return true;
}

template <typename... Nodes>
auto json_parser(Nodes&&... nodes) {
  return Parser<Nodes...>(std::forward<Nodes>(nodes)...);
}

// Target Bindings
template <typename PathT, typename TargetT>
auto bind(PathT p, TargetT& target) {
  return BindNode<PathT, TargetT>(p, target);
}

// Overload for root keys
template <typename TargetT>
auto bind(const char* root_key, TargetT& target) {
  return bind(path(root_key), target);
}

// Dynamic Variant Bindings
template <typename PathT>
auto bind(PathT p) {
  return DynamicNode<PathT>(p);
}

inline auto bind(const char* root_key) {
  return bind(path(root_key));
}

}  // namespace sjson