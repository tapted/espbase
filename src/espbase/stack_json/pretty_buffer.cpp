#include "espbase/stack_json/pretty_buffer.hpp"

void sjson::PrettyBuffer::write_indent() {
  for (uint8_t i = 0; i < depth_ * indent_size_; ++i) {
    dest_.write(" ");
  }
}

size_t sjson::PrettyBuffer::write(std::string_view str) {
  const size_t before = dest_.length();
  std::size_t start = 0;

  for (std::size_t i = 0; i < str.size(); ++i) {
    char c = str[i];

    // If we are inside a string, ignore all structural characters
    if (in_string_) {
      if (escape_next_) {
        escape_next_ = false;
      } else if (c == '\\') {
        escape_next_ = true;
      } else if (c == '"') {
        in_string_ = false;
      }
      continue;
    }

    // If we hit a structural character, format it!
    if (c == '{' || c == '[' || c == '}' || c == ']' || c == ',' || c == ':' || c == '"') {
      // Flush whatever non-structural characters we've accumulated so far (like numbers)
      if (i > start) {
        if (dest_.write(str.substr(start, i - start)) == 0) return 0;
      }
      start = i + 1;

      if (c == '"') {
        in_string_ = true;
        dest_.write("\"");
      } else if (c == '{' || c == '[') {
        dest_.write(c == '{' ? "{\n" : "[\n");
        depth_++;
        write_indent();
      } else if (c == '}' || c == ']') {
        dest_.write("\n");
        if (depth_ > 0) depth_--;
        write_indent();
        dest_.write(c == '}' ? "}" : "]");
      } else if (c == ',') {
        dest_.write(",\n");
        write_indent();
      } else if (c == ':') {
        dest_.write(": ");  // Add that nice space after the colon
      }
    }
  }

  // Flush the remaining chunk
  if (start < str.size()) {
    if (dest_.write(str.substr(start)) == 0) return 0;
  }
  return length() - before;
}
