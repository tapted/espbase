#include "espbase/stack_json/printer.hpp"

#include <cstdarg>
#include <cstdio>
#include <span>

#include "espbase/stack_json/buffer.hpp"

bool sjson::Printer::operator()(const char* fmt, ...) {
  if (!success_) return false;

  // 1. Write opening quote
  if (!buffer_.write("\"")) return success_ = false;

  std::span<char> span = buffer_.get_write_span();
  if (span.empty()) return success_ = false;

  // 2. Format directly into the buffer tail
  va_list args;
  va_start(args, fmt);
  int written = std::vsnprintf(span.data(), span.size(), fmt, args);
  va_end(args);

  if (written < 0 || static_cast<std::size_t>(written) >= span.size()) {
    return success_ = false;  // Overflow or format error
  }

  std::size_t L = written;
  std::size_t E = 0;

  // 3. First pass: Count extra characters needed for escaping
  for (std::size_t i = 0; i < L; ++i) {
    uint8_t c = span[i];
    if (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') {
      E += 1;
    } else if (c <= 0x1F) {
      E += 5;  // Needs \u00XX
    }
  }

  // 4. Second pass: Backwards in-place expansion!
  if (E > 0) {
    if (L + E > span.size()) return success_ = false;

    std::size_t read_idx = L - 1;
    std::size_t write_idx = L + E - 1;

    while (true) {
      uint8_t c = span[read_idx];
      if (c == '"' || c == '\\' || c == '\b' || c == '\f' || c == '\n' || c == '\r' || c == '\t') {
        switch (c) {
          case '"':
            span[write_idx--] = '"';
            break;
          case '\\':
            span[write_idx--] = '\\';
            break;
          case '\b':
            span[write_idx--] = 'b';
            break;
          case '\f':
            span[write_idx--] = 'f';
            break;
          case '\n':
            span[write_idx--] = 'n';
            break;
          case '\r':
            span[write_idx--] = 'r';
            break;
          case '\t':
            span[write_idx--] = 't';
            break;
        }
        span[write_idx--] = '\\';
      } else if (c <= 0x1F) {
        char hex[7];
        std::snprintf(hex, sizeof(hex), "\\u%04x", c);
        for (int j = 5; j >= 0; --j) span[write_idx--] = hex[j];
      } else {
        span[write_idx--] = c;
      }
      if (read_idx == 0) break;
      read_idx--;
    }
  }

  // 5. Commit the final length and close the quote
  buffer_.commit(L + E);
  return success_ = buffer_.write("\"");
}
