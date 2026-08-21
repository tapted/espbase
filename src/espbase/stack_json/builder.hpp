#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/document.hpp"

namespace sjson {

// Allows building a new JSON document from multiple existing documents on the stack.
class BuilderBase {
 protected:
  std::span<NodeBase*> storage_;
  std::size_t count_ = 0;

  // Protected constructor so this can only be instantiated via a subclass
  explicit BuilderBase(std::span<NodeBase*> storage) : storage_(storage) {}

 public:
  // Delete copy/move to prevent accidental slicing or dangling spans
  BuilderBase(const BuilderBase&) = delete;
  BuilderBase& operator=(const BuilderBase&) = delete;

  // The ingest method remains here so it can accept any Document
  template <typename... Nodes>
  BuilderBase& add(Document<Nodes...>& doc) {
    doc.append_pointers_to(storage_, count_);
    return *this;
  }

  void emit(Buffer& buffer) const {
    // subspan(0, count_) creates a view of only the valid, populated pointers
    emit_json_nodes(buffer, storage_.subspan(0, count_));
  }
};

template <std::size_t MaxSize>
class Builder : public BuilderBase {
  std::array<NodeBase*, MaxSize> array_;

 public:
  // Initialize the base class with a span covering our local stack array
  Builder() : BuilderBase(array_) {}
};

}  // namespace sjson