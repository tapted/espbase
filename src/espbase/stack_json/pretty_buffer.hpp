#pragma once

#include "espbase/stack_json/buffer.hpp"

namespace sjson {

/**
 * @brief Decorator buffer that formats output as human-readable indented JSON.
 *
 * @details Wraps any `Buffer` (such as `StackBuffer` or `DynamicBuffer`) and
 * intercepts `write()` calls to insert newlines and spaces according to JSON
 * brace/bracket nesting depth. Because it is completely decoupled from the
 * generation tree, unused binaries omit it via dead-code elimination.
 *
 * @code{.cpp}
 * sjson::StackBuffer<256> raw_buffer;
 * sjson::PrettyBuffer pretty(raw_buffer, 2);  // 2 spaces per indent level
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("name", "espuck"),
 *     sjson::node("online", true)
 * );
 * doc.emit(pretty);
 * // raw_buffer.c_str() now holds formatted, indented JSON
 * @endcode
 */
class PrettyBuffer : public Buffer {
  Buffer& dest_;
  uint8_t indent_size_;
  uint8_t depth_ = 0;
  bool in_string_ = false;
  bool escape_next_ = false;

  void write_indent();

 public:
  /**
   * @brief Constructs a PrettyBuffer wrapping @p dest.
   *
   * @param dest Underlying destination buffer that receives the formatted bytes.
   * @param indent_size Number of space characters per indentation level (default: 2).
   */
  constexpr PrettyBuffer(Buffer& dest, uint8_t indent_size = 2)
      : dest_(dest), indent_size_(indent_size) {}

  /// @brief Writes @p str into the wrapped buffer with indentation and newlines inserted.
  size_t write(std::string_view str) override;

  /// @brief Delegates to the wrapped buffer's `get_write_span()`.
  std::span<char> get_write_span() override { return dest_.get_write_span(); }

  /// @brief Delegates to the wrapped buffer's `commit()`.
  void commit(std::size_t count) override { dest_.commit(count); }

  /// @brief Delegates to the wrapped buffer's `head()`.
  char* head() override { return dest_.head(); }

  /// @brief Delegates to the wrapped buffer's `length()`.
  size_t length() const override { return dest_.length(); }
};

}  // namespace sjson