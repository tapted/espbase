#pragma once

namespace sjson {
class Buffer;

class Printer {
  Buffer& buffer_;
  bool success_ = true;

 public:
  explicit Printer(Buffer& b) : buffer_(b) {}

  // Overload operator() to give the user that sweet printf syntax
  bool operator()(const char* fmt, ...);
};

}  // namespace sjson