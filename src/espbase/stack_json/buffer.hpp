#pragma once

#include <algorithm>
#include <span>
#include <string_view>

namespace sjson {

// Abstract buffer interface
class Buffer {
 public:
  constexpr Buffer() = default;
  virtual ~Buffer() = default;
  size_t write_escaped(std::string_view str);

  std::string_view last_write(size_t written) {
    return std::string_view(head() - written, written);
  }

  virtual size_t write(std::string_view str) = 0;
  virtual std::span<char> get_write_span();
  virtual void commit(std::size_t count);
  virtual char* head() = 0;
  virtual size_t length() const = 0;
};

template <std::size_t MaxSize>
class StackBuffer : public Buffer {
  char buffer_[MaxSize + 1] = {};  // +1 so we can null-terminate for c_str()
  std::size_t head_ = 0;

 public:
  constexpr StackBuffer() = default;

  void reset() { head_ = 0; }

  size_t write(std::string_view str) override {
    if (head_ + str.size() > MaxSize) return 0;
    std::copy(str.begin(), str.end(), buffer_ + head_);
    head_ += str.size();
    return str.size();
  }
  std::string_view view() const { return {buffer_, head_}; }

  const char* c_str() {
    buffer_[head_] = '\0';
    return buffer_;
  }

  std::span<char> get_write_span() override {
    if (head_ >= MaxSize) return {};
    return std::span<char>(buffer_ + head_, MaxSize - head_);
  }

  void commit(std::size_t count) override { head_ += count; }
  char* head() override { return buffer_ + head_; }
  size_t length() const override { return head_; }
};

}  // namespace sjson