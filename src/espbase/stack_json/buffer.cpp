#include "espbase/stack_json/buffer.hpp"

#include <cstdio>

namespace sjson {

std::span<char> Buffer::get_write_span() {
  return {};
}
void Buffer::commit(std::size_t /*count*/) {
}

size_t Buffer::write_escaped(std::string_view str) {
  const size_t before = length();
  if (write("\"") == 0) return 0;  // Open quote

  std::size_t start = 0;
  for (std::size_t i = 0; i < str.size(); ++i) {
    // Cast to uint8_t to prevent signed char traps with UTF-8 high bytes
    uint8_t c = static_cast<uint8_t>(str[i]);

    // Escape only quotes, backslashes, and ASCII control chars (0x00 - 0x1F)
    // UTF-8 characters (like 0xC2) are > 0x1F and pass through safely!
    if (c == '"' || c == '\\' || c <= 0x1F) {
      // Flush the "safe" characters we've scanned so far
      if (i > start) {
        if (write(str.substr(start, i - start)) == 0) return 0;
      }

      // Write the appropriate escape sequence
      bool ok = true;
      switch (c) {
        case '"':
          ok = write("\\\"");
          break;
        case '\\':
          ok = write("\\\\");
          break;
        case '\b':
          ok = write("\\b");
          break;
        case '\f':
          ok = write("\\f");
          break;
        case '\n':
          ok = write("\\n");
          break;
        case '\r':
          ok = write("\\r");
          break;
        case '\t':
          ok = write("\\t");
          break;
        default:
          // Hex encode remaining control chars (e.g., \u001B for Escape)
          char hex_buf[7];
          std::snprintf(hex_buf, sizeof(hex_buf), "\\u%04x", static_cast<unsigned char>(c));
          ok = write(std::string_view(hex_buf, 6));
          break;
      }

      if (!ok) return 0;
      start = i + 1;  // Move the start pointer past the escaped character
    }
  }

  // Flush any remaining safe characters
  if (start < str.size()) {
    if (write(str.substr(start)) == 0) return 0;
  }

  if (write("\"") == 0) return 0;  // Close quote
  return length() - before;
}
}  // namespace sjson