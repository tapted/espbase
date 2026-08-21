#pragma once

#include <algorithm>
#include <string_view>

namespace sjson {

// Abstract buffer interface
class Buffer {
 public:
  constexpr Buffer() = default;
  virtual ~Buffer() = default;
  virtual bool write(std::string_view str) = 0;
  bool write_escaped(std::string_view str);
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
};

}  // namespace sjson