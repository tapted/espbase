#pragma once

#include <array>
#include <string_view>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/path.hpp"

namespace sjson {

enum class EmitResult { OK, HAS_CHILDREN, SKIP, BUFFER_FULL };

class NodeBase {
 protected:
  bool emitted_ = false;

 public:
  virtual ~NodeBase() = default;
  void reset() { emitted_ = false; }
  bool is_emitted() const { return emitted_; }

  virtual EmitResult emit(const PathBase& open_parent, bool is_first_child, Buffer& buffer) = 0;
};

// Abstract implementation for string values
class StringNodeBase : public NodeBase {
 protected:
  std::string_view value_view_;  // Points to the concrete class's storage
 public:
  EmitResult emit(const PathBase& open_parent, bool is_first_child, Buffer& buffer) override {
    // Implementation of key-value emission goes here, checking `open_parent`.
    // Sets `emitted_ = true` when done.
    return EmitResult::OK;
  }
};

// Concrete template holding the data safely on the stack
template <typename PathT, std::size_t MaxLen>
class StringNode : public StringNodeBase {
  PathT path_;
  std::array<char, MaxLen> storage_;

 public:
  StringNode(PathT p, std::string_view val) : path_(p) {
    std::size_t len = std::min(val.size(), MaxLen);
    std::copy(val.begin(), val.begin() + len, storage_.begin());
    value_view_ = std::string_view(storage_.data(), len);
  }
};

// Node builder
template <typename PathT>
auto node(PathT path, const char* val) {
  return StringNode<PathT, 32>(path, val);  // 32 is arbitrary for this example
}

}  // namespace sjson