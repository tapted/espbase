#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/document.hpp"

namespace sjson {

// Allows building a new JSON document from multiple existing documents on the stack.
class Builder {
 protected:
  std::span<NodeBase*> storage_;
  std::size_t count_ = 0;

  // Protected constructor so this can only be instantiated via a subclass
  explicit Builder(std::span<NodeBase*> storage) : storage_(storage) {}

 public:
  // Delete copy/move to prevent accidental slicing or dangling spans
  Builder(const Builder&) = delete;
  Builder& operator=(const Builder&) = delete;

  // The ingest method remains here so it can accept any Document
  template <typename... Nodes>
  Builder& add(Document<Nodes...>& doc) {
    doc.append_pointers_to(storage_, count_);
    return *this;
  }

  bool emit(Buffer& buffer) const {
    // subspan(0, count_) creates a view of only the valid, populated pointers
    return emit_json_nodes(buffer, storage_.subspan(0, count_));
  }
};

template <std::size_t MaxSize>
class StackBuilder : public Builder {
  std::array<NodeBase*, MaxSize> array_;

 public:
  // Initialize the base class with a span covering our local stack array
  StackBuilder() : Builder(array_) {}
};

}  // namespace sjson