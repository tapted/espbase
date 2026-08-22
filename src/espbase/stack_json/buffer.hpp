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
  bool write_escaped(std::string_view str);

  virtual bool write(std::string_view str) = 0;
  virtual std::span<char> get_write_span();
  virtual void commit(std::size_t count);
};

template <std::size_t MaxSize>
class StackBuffer : public Buffer {
  char buffer_[MaxSize + 1] = {};  // +1 so we can null-terminate for c_str()
  std::size_t head_ = 0;

 public:
  constexpr StackBuffer() = default;

  void reset() { head_ = 0; }

  bool write(std::string_view str) override {
    if (head_ + str.size() > MaxSize) return false;
    std::copy(str.begin(), str.end(), buffer_ + head_);
    head_ += str.size();
    return true;
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
};

}  // namespace sjson