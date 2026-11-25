#pragma once

#include <utility>

#include "espbase/stack_json/buffer.hpp"

namespace sjson {

class Buffer;

/**
 * @brief Streaming array writer passed to `stream_array()` and inline array callbacks.
 *
 * @details Automatically inserts comma separators between elements and propagates
 * buffer overflow failures. Users never construct `ArrayEmitter` directly — it is
 * passed by reference into `stream_array([&](auto& out) { ... })` or inline
 * `node("key", [&](sjson::ArrayEmitter& out) { ... })` callbacks.
 *
 * @code{.cpp}
 * auto doc = sjson::stack_json(
 *     sjson::node("values", sjson::stream_array([&](sjson::ArrayEmitter& out) {
 *         for (int i = 0; i < 10; ++i) {
 *             if (!out.emit(i * 10)) break;  // Stop early if buffer fills
 *         }
 *     }))
 * );
 * @endcode
 */
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
  /// @brief Internal constructor called by `StreamArrayDocument` and `write_json_value`.
  explicit ArrayEmitter(Buffer& b) : buffer_(b) {}

  /**
   * @brief Emits a single element into the array.
   *
   * @details Inserts a leading `,` if this is not the first element, then writes
   * @p value (which may be a scalar, string, `stack_json()` document, nested
   * array, or callback).
   *
   * @tparam T Any type supported by `sjson::node()`.
   * @param value The element to emit.
   * @return `true` if the write succeeded, `false` if the underlying buffer failed
   *         (e.g. fixed-capacity `StackBuffer` overflow).
   */
  template <typename T>
  bool emit(T&& value);

  /**
   * @brief Function-call shorthand for `emit(value)`.
   *
   * @tparam T Any type supported by `sjson::node()`.
   * @param value The element to emit.
   * @return `true` on success, `false` on buffer failure.
   */
  template <typename T>
  bool operator()(T&& value) {
    return emit(std::forward<T>(value));
  }

  /**
   * @brief Returns `true` if the underlying buffer is healthy (not in a failed state).
   *
   * @return `!buffer_.bad()`.
   */
  bool ok() const { return !buffer_.bad(); }

  /// @brief Returns a reference to the underlying `Buffer`.
  Buffer& buffer() { return buffer_; }
};

}  // namespace sjson
