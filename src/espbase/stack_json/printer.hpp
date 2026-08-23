#pragma once

#include <cstdarg>

namespace sjson {
class Buffer;

// A Printer is a utility class that allows printf-style formatting into a Buffer, with optional
// escaping for JSON strings.
class Printer {
  Buffer& buffer_;
  bool success_ = true;

  bool vprint(bool escaped, const char* fmt, va_list args);

 public:
  explicit Printer(Buffer& b) : buffer_(b) {}

  // Overload the function call operator to accept printf-style formatting *with* escaping for JSON
  // strings. Does not include the outer quotes so it can be called multiple times.
  bool operator()(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  // Writes printf-style formatting directly into the buffer without escaping.
  bool write_raw(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  // Helper to print (escaped) directly into a buffer without creating a Printer object.
  static bool printq(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  // Helper to print (raw) directly into a buffer without creating a Printer object.
  static bool printx(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
};

}  // namespace sjson