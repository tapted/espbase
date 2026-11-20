#pragma once

#include <algorithm>
#include <string_view>
#include <utility>

#include "espbase/stack_json/parser_impl.hpp"
#include "espbase/stack_json/path.hpp"

namespace sjson {

class ParseNodeBase {
 protected:
  PathBase path_{};
  bool is_set_ = false;
  bool is_null_ = false;
  bool is_catch_all_ = false;

  explicit constexpr ParseNodeBase(bool catch_all = false) : is_catch_all_(catch_all) {}
  ~ParseNodeBase() = default;

 public:
  const PathBase& path() const { return path_; }
  // Whether this node is a catch-all (matches any path). Used for dynamic nodes.
  bool is_catch_all() const { return is_catch_all_; }

  ParseNodeBase* init_node() { return this; }

  // The parser will call this when it finds a matching path
  virtual void assign(const PathBase& actual_path, std::string_view raw_val, bool is_string,
                      bool is_null) = 0;

  bool is_set() const { return is_set_; }
  bool is_null() const { return is_null_; }

  void reset() { is_set_ = is_null_ = false; }
};

template <typename TargetT>
class ParseNodeBaseT : public ParseNodeBase {
  TargetT& target_;

 public:
  explicit constexpr ParseNodeBaseT(TargetT& target) : ParseNodeBase(false), target_(target) {}

  void assign(const PathBase&, std::string_view raw_val, bool, bool is_null) override {
    is_set_ = true;
    is_null_ = is_null;
    coerce_value(raw_val, is_null, target_);
  }
};

template <typename PathT, typename TargetT>
class BindNode : public ParseNodeBaseT<TargetT> {
  PathT static_path_;

 public:
  static constexpr std::size_t static_depth = PathT::static_depth;

  template <typename P>
  explicit constexpr BindNode(P&& p, TargetT& t)
      : ParseNodeBaseT<TargetT>(t), static_path_(std::forward<P>(p)) {}

  ParseNodeBase* init_node() {
    this->path_ = static_path_;
    return this;
  }
};

template <typename PathT>
class DynamicNode : public DynamicNodeBase {
  PathT static_path_;

 public:
  static constexpr std::size_t static_depth = PathT::static_depth;
  template <typename P>
  explicit DynamicNode(P&& p) : DynamicNodeBase(false), static_path_(std::forward<P>(p)) {}

  ParseNodeBase* init_node() {
    this->path_ = static_path_;
    return this;
  }
};

class UnknownBindNodeBase : public DynamicNodeBase {
 public:
  static constexpr std::size_t static_depth = 0;
  UnknownBindNodeBase() : DynamicNodeBase(true) {}

  ParseNodeBase* init_node() { return this; }
};

template <typename Callback>
class UnknownBindNode : public UnknownBindNodeBase {
  // Could this use std::function_ref once esp-idf supports it? (needs gcc 16).
  Callback cb_;

 public:
  explicit constexpr UnknownBindNode(Callback cb) : cb_(std::move(cb)) {}
  void assign(const PathBase& actual_path, std::string_view raw_val, bool is_string,
              bool is_null) override {
    DynamicNodeBase::assign(actual_path, raw_val, is_string, is_null);
    cb_(actual_path, *this);  // Fire the callback, passing *this as the DynamicNodeBase
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
          return std::array<ParseNodeBase*, sizeof...(Nodes)>{std::get<I>(nodes_).init_node()...};
        }(std::make_index_sequence<sizeof...(Nodes)>{});

    std::array<std::string_view, MaxDepth> path_stack;
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

}  // namespace sjson
