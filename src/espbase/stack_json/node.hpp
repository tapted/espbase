#pragma once

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/value.hpp"

namespace sjson {
  
class NodeBase {
 protected:
  bool emitted_ = false;
  bool omitted_ = false;

 public:
  virtual ~NodeBase() = default;
  void reset() { emitted_ = false; }
  bool is_emitted() const { return emitted_; }
  bool is_omitted() const { return omitted_; }
  void set_omitted(bool o) { omitted_ = o; }

  virtual const PathBase& path() const = 0;
  virtual void emit_value(Buffer& buffer) = 0;
};

template <typename PathT, typename ValueT>
class ValueNode : public NodeBase {
  PathT path_;
  ValueT value_;

 public:
  ValueNode(PathT p, ValueT v) : path_(p), value_(std::move(v)) {}

  const PathBase& path() const override { return path_; }

  void emit_value(Buffer& buffer) override {
    write_json_value(buffer, value_);
    emitted_ = true;
  }
};

template <typename PathT, typename ValueT>
auto node(PathT p, ValueT val) {
  return ValueNode<PathT, ValueT>(p, std::move(val));
}

template <typename ValueT>
auto node(const char* root_key, ValueT val) {
  return node(path(root_key), std::move(val));
}

template <typename PathT, typename ValueT>
auto node_if(bool condition, PathT p, ValueT val) {
  auto n = node(p, std::move(val));
  n.set_omitted(!condition);
  return n;
}

template <typename PathT>
auto node_if(PathT p, const char* val) {
  return node_if(val != nullptr, p, val);
}

}  // namespace sjson