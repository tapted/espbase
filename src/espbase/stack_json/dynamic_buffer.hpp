#pragma once

#include <algorithm>
#include <span>
#include <string_view>
#include <vector>

#include "espbase/psram_allocator.h"
#include "espbase/stack_json/buffer.hpp"

namespace sjson {

// A dynamic buffer that grows as needed. It uses a std::vector by default with a PSRAM allocator,
// but you can provide your own container type if desired. The buffer will always maintain a
// "watermark" of ExpandSize bytes available for printf-style expansion.
template <typename Container = std::vector<char, PSRAMAllocator<char>>,
          std::size_t ExpandSize = 256>
class DynamicBuffer : public Buffer {
  Container buffer_;
  std::size_t head_ = 0;

 public:
  DynamicBuffer() {
    // Pre-allocate initial space to prevent early reallocations
    buffer_.resize(ExpandSize);
  }

  void reset() { head_ = 0; }

  size_t write(std::string_view str) override {
    // Grow if we don't have enough room
    if (head_ + str.size() > buffer_.size()) {
      buffer_.resize(head_ + str.size() + ExpandSize);
    }

    std::copy(str.begin(), str.end(), buffer_.begin() + head_);
    head_ += str.size();
    return str.size();
  }

  std::span<char> get_write_span() override {
    // Ensure our "watermark" (ExpandSize) is available for printf expansion
    if (buffer_.size() - head_ < ExpandSize) {
      buffer_.resize(head_ + ExpandSize);
    }
    return std::span<char>(buffer_.data() + head_, buffer_.size() - head_);
  }

  void commit(std::size_t count) override { head_ += count; }
  char* head() override { return buffer_.data() + head_; }
  size_t length() const override { return head_; }

  std::string_view view() const { return {buffer_.data(), head_}; }

  const char* c_str() {
    // Guarantee exactly one byte for the null terminator
    if (head_ == buffer_.size()) {
      buffer_.resize(head_ + 1);
    }
    buffer_[head_] = '\0';
    return buffer_.data();
  }

  // Expose the underlying container so you can std::move it out if needed
  Container& container() { return buffer_; }
};

}  // namespace sjson