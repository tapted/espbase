#pragma once

#include <cstdarg>
#include <ctime>
#include <string_view>
#include <type_traits>

namespace sjson {
class Buffer;

/**
 * @brief `printf`-style formatter that writes directly into a `Buffer` with optional JSON string escaping.
 *
 * @details Users receive a `Printer&` in `printf`-style `node()` callbacks:
 * @code{.cpp}
 * sjson::node("topic", [&](auto& print) {
 *     print("home/sensor/%s/state", sensor_id);
 * })
 * @endcode
 * Calling `print(...)` formats directly into the buffer's unused tail and applies
 * JSON escaping in-place without intermediate buffers. Static helpers (`printq()`,
 * `printx()`, `printt()`, `print_utctime()`) are also available for direct use
 * against a `Buffer&` outside of `node()` callbacks.
 */
class Printer {
  Buffer& buffer_;

  static size_t write(Buffer& buffer, std::string_view str);
  size_t vprint(bool escaped, const char* fmt, va_list args);

 public:
  /// @brief Constructs a `Printer` writing into @p b.
  explicit Printer(Buffer& b) : buffer_(b) {}

  /**
   * @brief Formats a `printf`-style string directly into the buffer with JSON escaping.
   *
   * @details Does not emit outer `"` quotes, allowing multiple calls in one callback
   * to append cleanly into the same JSON string value.
   *
   * @param fmt `printf` format string.
   * @return Number of bytes written (after escaping), or 0 on failure.
   */
  size_t operator()(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   * @brief Formats a `printf`-style string directly into the buffer without escaping.
   *
   * @param fmt `printf` format string.
   * @return Number of bytes written, or 0 on failure.
   */
  size_t write_raw(const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   * @brief Static helper to `printf` (with JSON escaping) directly into @p buffer.
   *
   * @param buffer Destination buffer.
   * @param fmt `printf` format string.
   * @return Number of bytes written (after escaping), or 0 on failure.
   */
  static size_t printq(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   * @brief Static helper to `printf` (raw, unescaped) directly into @p buffer.
   *
   * @param buffer Destination buffer.
   * @param fmt `printf` format string.
   * @return Number of bytes written, or 0 on failure.
   */
  static size_t printx(Buffer& buffer, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

  /**
   * @brief Formats a `time_t` timestamp as an ISO-8601 UTC string into @p buffer.
   *
   * @param buffer Destination buffer.
   * @param time Unix epoch timestamp.
   * @return Number of bytes written, or 0 on failure.
   */
  static size_t print_utctime(Buffer& buffer, time_t time);

  /**
   * @brief Generic scalar helper that formats strings, integers, or floats into @p buffer.
   *
   * @tparam T A string-convertible type, integral type, or floating-point type.
   * @param buffer Destination buffer.
   * @param value The value to format.
   * @return Number of bytes written, or 0 on failure.
   */
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