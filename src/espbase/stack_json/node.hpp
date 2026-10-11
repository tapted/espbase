#pragma once

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/value.hpp"

namespace sjson {

class NodeBase {
 protected:
  const PathBase* path_ptr_;
  bool emitted_ = false;
  bool omitted_ = false;

  NodeBase(const NodeBase& other, const PathBase* new_path)
      : path_ptr_(new_path), emitted_(other.emitted_), omitted_(other.omitted_) {}

 public:
  explicit NodeBase(const PathBase* path_ptr) : path_ptr_(path_ptr) {}
  virtual ~NodeBase() = default;

  // Delete base-level assignment to prevent object slicing
  NodeBase& operator=(const NodeBase&) = delete;
  NodeBase& operator=(NodeBase&&) = delete;

  void reset() { emitted_ = false; }
  bool is_emitted() const { return emitted_; }
  bool is_omitted() const { return omitted_; }
  void set_omitted(bool o) { omitted_ = o; }

  const PathBase& path() const { return *path_ptr_; }

  virtual void emit_value(Buffer& buffer) = 0;
};

template <typename PathT, typename ValueT>
class ValueNode : public NodeBase {
  PathT path_;
  ValueT value_;

 public:
  ValueNode(PathT p, ValueT&& v)
      : NodeBase(&path_), path_(std::move(p)), value_(std::forward<ValueT>(v)) {}
  ValueNode(const ValueNode& other)
      : NodeBase(other, &path_), path_(other.path_), value_(other.value_) {}
  ValueNode(ValueNode&& other)
      : NodeBase(other, &path_),
        path_(std::move(other.path_)),
        value_(std::forward<ValueT>(other.value_)) {}

  void emit_value(Buffer& buffer) override {
    write_json_value(buffer, value_);
    emitted_ = true;
  }
};

template <typename PathT, typename ValueT>
auto node(PathT p, ValueT&& val) {
  return ValueNode<PathT, ValueT>(p, std::forward<ValueT>(val));
}

template <typename ValueT>
auto node(const char* root_key, ValueT&& val) {
  return node(path(root_key), std::forward<ValueT>(val));
}

template <typename PathT, typename ValueT>
auto node_if(bool condition, PathT p, ValueT&& val) {
  auto n = node(p, std::forward<ValueT>(val));
  n.set_omitted(!condition);
  return n;
}

template <typename ValueT>
auto node_if(bool condition, const char* root_key, ValueT&& val) {
  return node_if(condition, path(root_key), std::forward<ValueT>(val));
}

template <typename PathT>
auto node_if(PathT p, const char* val) {
  // std::move forces ValueT to deduce as 'const char*' instead of 'const char*&'
  // This guarantees the node copies the pointer itself, rather than referencing the local variable.
  return node_if(val != nullptr, p, std::move(val));
}

inline auto node_if(const char* root_key, const char* val) {
  return node_if(val != nullptr, path(root_key), std::move(val));
}

}  // namespace sjson