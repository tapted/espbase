#pragma once

#include <cstdarg>
#include <string_view>
#include <type_traits>

namespace sjson {
class Buffer;

// A Printer is a utility class that allows printf-style formatting into a Buffer, with optional
// escaping for JSON strings.
class Printer {
  Buffer& buffer_;
  bool success_ = true;

  static size_t write(Buffer& buffer, std::string_view str);
  size_t vprint(bool escaped, const char* fmt, va_list args);

 public:
  explicit Printer(Buffer& b) : buffer_(b) {}

  // Overload the function call operator to accept printf-style formatting *with* escaping for JSON
  // strings. Does not include the outer quotes so it can be called multiple times.
  size_t operator()(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  // Writes printf-style formatting directly into the buffer without escaping.
  size_t write_raw(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  // Helper to print (escaped) directly into a buffer without creating a Printer object.
  static size_t printq(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
  // Helper to print (raw) directly into a buffer without creating a Printer object.
  static size_t printx(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  template <typename T>
  static size_t printt(Buffer& buffer, T value) {
    if constexpr (std::is_convertible_v<T, std::string_view>) {
      return write(buffer, std::string_view(value));
    } else if constexpr (std::is_integral_v<T>) {
      if constexpr (std::is_signed_v<T>) {
        return printx(buffer, "%lld", static_cast<long long>(value));
      } else {
        return printx(buffer, "%llu", static_cast<unsigned long long>(value));
      }
    } else if constexpr (std::is_floating_point_v<T>) {
      return printx(buffer, "%g", static_cast<double>(value));
    } else {
      static_assert(false, "Unsupported type for printt");
    }
  }
};

}  // namespace sjson