#pragma once

#include "espbase/stack_json/buffer.hpp"

namespace sjson {

class PrettyBuffer : public Buffer {
  Buffer& dest_;
  uint8_t indent_size_;
  uint8_t depth_ = 0;
  bool in_string_ = false;
  bool escape_next_ = false;

  void write_indent();

 public:
  constexpr PrettyBuffer(Buffer& dest, uint8_t indent_size = 2)
      : dest_(dest), indent_size_(indent_size) {}

  bool write(std::string_view str) override;
};

}  // namespace sjson