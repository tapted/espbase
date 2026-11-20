#pragma once

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/value.hpp"

namespace sjson {

class NodeBase {
 protected:
  bool emitted_ = false;
  bool omitted_ = false;

  void on_emit() { emitted_ = true; }

 protected:
  ~NodeBase() = default;

 public:
  void reset() { emitted_ = false; }

  bool is_emitted() const { return emitted_; }
  bool is_omitted() const { return omitted_; }
  void set_omitted(bool o) { omitted_ = o; }

  virtual PathBase path() const = 0;
  virtual void emit_value(Buffer& buffer) = 0;
};

template <typename ValueT>
class ValueNodeBaseT : public NodeBase {
 protected:
  ValueT value_;

 public:
  template <typename V>
  explicit ValueNodeBaseT(V&& v) : value_(std::forward<V>(v)) {}

  void emit_value(Buffer& buffer) override {
    write_json_value(buffer, value_);
    on_emit();
  }
};

template <typename PathT, typename ValueT>
class ValueNode : public ValueNodeBaseT<ValueT> {
  PathT path_;

 public:
  template <typename P, typename V>
  ValueNode(P&& p, V&& v) : ValueNodeBaseT<ValueT>(std::forward<V>(v)), path_(std::forward<P>(p)) {}

  PathBase path() const override { return path_; }
};

template <typename T>
using node_value_t =
    std::conditional_t<std::is_array_v<std::remove_reference_t<T>>, std::decay_t<T>, T>;

}  // namespace sjson
