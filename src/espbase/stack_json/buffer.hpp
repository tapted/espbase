#pragma once

#include <algorithm>
#include <array>
#include <string_view>

namespace sjson {

// Abstract buffer interface
class Buffer {
 public:
  virtual ~Buffer() = default;
  virtual bool write(std::string_view str) = 0;
  bool write_escaped(std::string_view str);
};

template <std::size_t MaxSize>
class StackBuffer : public Buffer {
  std::array<char, MaxSize> buffer_;
  std::size_t head_ = 0;

 public:
  bool write(std::string_view str) override {
    if (head_ + str.size() > MaxSize) return false;
    std::copy(str.begin(), str.end(), buffer_.begin() + head_);
    head_ += str.size();
    return true;
  }
  std::string_view view() const { return {buffer_.data(), head_}; }
};

}  // namespace sjson