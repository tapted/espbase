#pragma once

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

#include "espbase/psram_allocator.h"
#include "espbase/stack_json/buffer.hpp"

namespace sjson {

/**
 * @brief Heap/PSRAM-backed dynamically-growing output buffer.
 *
 * @details Implements the `Buffer` interface on top of a contiguous STL-compatible
 * container (defaulting to `std::vector<char, PSRAMAllocator<char>>` on ESP32, or
 * `std::vector<char>` on standard heap).
 *
 * The buffer maintains a "watermark" of at least @p ExpandSize free bytes at the
 * end of the container whenever `get_write_span()` is called. This guarantees
 * contiguous scratch space for zero-copy `printf`-style expansion and in-place
 * JSON escaping without out-of-bounds writes.
 *
 * @tparam Container Contiguous byte container type supporting `resize()`, `data()`,
 *                   `size()`, and `begin()`. Defaults to `std::vector<char, PSRAMAllocator<char>>`.
 * @tparam ExpandSize Minimum free-space watermark (in bytes) maintained at the tail
 *                    for `get_write_span()`. Defaults to 256 bytes.
 *
 * @code{.cpp}
 * // Default — routes to ESP32 PSRAM using PSRAMAllocator
 * sjson::DynamicBuffer<> psram_buf;
 *
 * // Or use standard heap allocation
 * sjson::DynamicBuffer<std::vector<char>> heap_buf;
 *
 * auto doc = sjson::stack_json(
 *     sjson::node("device_class", "temperature"),
 *     sjson::node("state_topic", [&](auto& print) {
 *         print("home/sensor/%s", config.identifier);
 *     })
 * );
 *
 * doc.emit(psram_buf);
 * mqtt_client.publish(psram_buf.c_str());
 * @endcode
 */
template <typename Container = std::vector<char, PSRAMAllocator<char>>,
          std::size_t ExpandSize = 256>
class DynamicBuffer : public Buffer {
  Container buffer_;
  std::size_t head_ = 0;

 public:
  /**
   * @brief Constructs the buffer and pre-allocates @p ExpandSize bytes.
   */
  DynamicBuffer() {
    // Pre-allocate initial space to prevent early reallocations
    buffer_.resize(ExpandSize);
  }

  /**
   * @brief Resets the write cursor to 0 and clears any failure flag.
   *
   * @details Does not shrink or deallocate the underlying container's capacity,
   * making buffer reuse across emissions allocation-free.
   */
  void reset() {
    clear_fail();
    head_ = 0;
  }

  /**
   * @brief Appends @p str to the buffer, growing the container if needed.
   *
   * @param str The bytes to write.
   * @return Number of bytes written.
   */
  size_t write(std::string_view str) override {
    // Grow if we don't have enough room
    if (head_ + str.size() > buffer_.size()) {
      buffer_.resize(head_ + str.size() + ExpandSize);
    }

    std::copy(str.begin(), str.end(), buffer_.begin() + head_);
    head_ += str.size();
    return str.size();
  }

  /**
   * @brief Returns a writable span guaranteed to have at least @p ExpandSize bytes.
   *
   * @details Resizes the underlying container if remaining tail space is below
   * @p ExpandSize. Call `commit()` with the number of bytes actually written.
   *
   * @return Writable `std::span<char>` pointing to the unused tail.
   */
  std::span<char> get_write_span() override {
    // Ensure our "watermark" (ExpandSize) is available for printf expansion
    if (buffer_.size() - head_ < ExpandSize) {
      buffer_.resize(head_ + ExpandSize);
    }
    return std::span<char>(buffer_.data() + head_, buffer_.size() - head_);
  }

  /// @brief Advances the write cursor by @p count bytes after a direct write.
  void commit(std::size_t count) override { head_ += count; }

  /// @brief Returns a pointer to the next byte to be written.
  char* head() override { return buffer_.data() + head_; }

  /// @brief Returns the number of bytes written so far.
  size_t length() const override { return head_; }

  /**
   * @brief Returns a read-only view over the written bytes.
   *
   * @return `std::string_view` spanning `[0, length())`.
   */
  std::string_view view() const { return {buffer_.data(), head_}; }

  /**
   * @brief Null-terminates the written content and returns a C-string pointer.
   *
   * @details Grows the container by 1 byte if `head_ == buffer_.size()`. Does not
   * modify `length()`.
   *
   * @return Pointer to the null-terminated string inside the container.
   */
  const char* c_str() {
    // Guarantee exactly one byte for the null terminator
    if (head_ == buffer_.size()) {
      buffer_.resize(head_ + 1);
    }
    buffer_[head_] = '\0';
    return buffer_.data();
  }

  /**
   * @brief Returns a mutable reference to the underlying container.
   *
   * @details Allows callers to `std::move` the storage out after emission.
   *
   * @return Reference to the backing `Container`.
   */
  Container& container() { return buffer_; }
};

}  // namespace sjson