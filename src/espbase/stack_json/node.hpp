#pragma once

#include <array>
#include <string_view>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"
#include "espbase/stack_json/value.hpp"

namespace sjson {
class NodeBase {
 protected:
  bool emitted_ = false;

 public:
  virtual ~NodeBase() = default;
  void reset() { emitted_ = false; }
  bool is_emitted() const { return emitted_; }

  virtual const PathBase& path() const = 0;
  virtual void emit_value(Buffer& buffer) = 0;
};

template <typename PathT, std::size_t MaxLen>
class StringNode : public NodeBase {
  PathT path_;
  std::array<char, MaxLen> storage_;
  std::string_view value_view_;

 public:
  StringNode(PathT p, std::string_view val) : path_(p) {
    std::size_t len = std::min(val.size(), MaxLen);
    std::copy(val.begin(), val.begin() + len, storage_.begin());
    value_view_ = std::string_view(storage_.data(), len);
  }
  const PathBase& path() const override { return path_; }
  void emit_value(Buffer& buffer) override {
    buffer.write_quoted(value_view_);
    emitted_ = true;  // Mark complete
  }
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

template <typename PathT>
auto node(PathT p, const char* val) {
  return StringNode<PathT, 32>(p, val);
}

}  // namespace sjson