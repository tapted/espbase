#pragma once

#include <utility>

#include "espbase/stack_json/buffer.hpp"

namespace sjson {

class Buffer;

// An ArrayEmitter is a streaming utility passed to callbacks for emitting JSON arrays.
// It automatically manages comma separators and checks buffer limits.
class ArrayEmitter {
  Buffer& buffer_;
  bool first_ = true;

  bool before_element() {
    if (buffer_.bad()) return false;
    if (!first_) {
      return buffer_.write(",") != 0;
    }
    first_ = false;
    return true;
  }

 public:
  explicit ArrayEmitter(Buffer& b) : buffer_(b) {}

  // Emits an element into the array (scalar, string, Document, or nested callback).
  // Returns false if buffer write failed (e.g., fixed-capacity buffer overflow).
  template <typename T>
  bool emit(T&& value);

  // Overload for function-call syntax: stream(value)
  template <typename T>
  bool operator()(T&& value) {
    return emit(std::forward<T>(value));
  }

  // Returns true if the underlying buffer is healthy (not in a failed state)
  bool ok() const {
    return !buffer_.bad();
  }

  Buffer& buffer() { return buffer_; }
};

}  // namespace sjson
