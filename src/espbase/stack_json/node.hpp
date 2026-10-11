#pragma once

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/value.hpp"

namespace sjson {

class NodeBase {
 private:
  PathBase cached_path_;  // Caches a safe (sliced) view of the derived path
  bool emitted_ = false;
  bool omitted_ = false;

  virtual PathBase get_path() const = 0;

 protected:
  void on_emit() { emitted_ = true; }

 public:
  virtual ~NodeBase() = default;

  void reset() {
    emitted_ = false;
    cached_path_ = get_path();  // Binds the path to avoid virtual dispatch during hot traversal.
  }

  bool is_emitted() const { return emitted_; }
  bool is_omitted() const { return omitted_; }
  void set_omitted(bool o) { omitted_ = o; }

  const PathBase& path() const { return cached_path_; }

  virtual void emit_value(Buffer& buffer) = 0;
};

template <typename PathT, typename ValueT>
class ValueNode : public NodeBase {
  PathT path_;
  ValueT value_;

  PathBase get_path() const override { return path_; }

 public:
  ValueNode(PathT p, ValueT&& v) : path_(std::move(p)), value_(std::forward<ValueT>(v)) {}

  void emit_value(Buffer& buffer) override {
    write_json_value(buffer, value_);
    on_emit();
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