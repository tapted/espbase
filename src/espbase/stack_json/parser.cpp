#include "espbase/stack_json/parser.hpp"

#include <cctype>

#include "espbase/stack_json/path.hpp"

namespace sjson {
namespace {

class DynamicPathView : public PathBase {
  std::span<const std::string_view> stack_;
  std::size_t depth_;
  std::string_view leaf_key_;

 public:
  DynamicPathView(std::span<const std::string_view> stack, std::size_t depth, std::string_view leaf)
      : stack_(stack), depth_(depth), leaf_key_(leaf) {}

  std::size_t depth() const override { return depth_ + 1; }

  std::string_view get_element(std::size_t index) const override {
    return (index == depth_) ? leaf_key_ : stack_[index];
  }
};

}  // namespace

std::size_t decode_json_string(std::string_view input, std::span<char> out_buffer) {
  std::size_t out_idx = 0;
  for (std::size_t i = 0; i < input.size() && out_idx < out_buffer.size(); ++i) {
    if (input[i] == '\\' && i + 1 < input.size()) {
      i++;  // Skip the slash
      switch (input[i]) {
        case '"':
          out_buffer[out_idx++] = '"';
          break;
        case '\\':
          out_buffer[out_idx++] = '\\';
          break;
        case 'n':
          out_buffer[out_idx++] = '\n';
          break;
        case 'r':
          out_buffer[out_idx++] = '\r';
          break;
        case 't':
          out_buffer[out_idx++] = '\t';
          break;
        case 'b':
          out_buffer[out_idx++] = '\b';
          break;
        case 'f':
          out_buffer[out_idx++] = '\f';
          break;
        case 'u': {
          // Ensure we have 4 characters left to read
          if (i + 4 < input.size()) {
            uint16_t codepoint = 0;
            bool valid_hex = true;

            for (int j = 1; j <= 4; ++j) {
              char hc = input[i + j];
              codepoint <<= 4;
              if (hc >= '0' && hc <= '9')
                codepoint |= (hc - '0');
              else if (hc >= 'a' && hc <= 'f')
                codepoint |= (hc - 'a' + 10);
              else if (hc >= 'A' && hc <= 'F')
                codepoint |= (hc - 'A' + 10);
              else {
                valid_hex = false;
                break;
              }
            }

            if (valid_hex) {
              i += 4;  // Consume the 4 hex chars

              // 1-byte ASCII (0x0000 - 0x007F)
              if (codepoint <= 0x7F) {
                if (out_idx < out_buffer.size())
                  out_buffer[out_idx++] = static_cast<char>(codepoint);
              }
              // 2-byte UTF-8 (0x0080 - 0x07FF)
              else if (codepoint <= 0x07FF) {
                if (out_idx + 1 < out_buffer.size()) {
                  out_buffer[out_idx++] = static_cast<char>(0xC0 | ((codepoint >> 6) & 0x1F));
                  out_buffer[out_idx++] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }
              }
              // 3-byte UTF-8 (0x0800 - 0xFFFF)
              else {
                if (out_idx + 2 < out_buffer.size()) {
                  out_buffer[out_idx++] = static_cast<char>(0xE0 | ((codepoint >> 12) & 0x0F));
                  out_buffer[out_idx++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
                  out_buffer[out_idx++] = static_cast<char>(0x80 | (codepoint & 0x3F));
                }
              }
              break;
            }
          }
          // Fallback if invalid hex or out of bounds
          out_buffer[out_idx++] = 'u';
          break;
        }
        default:
          out_buffer[out_idx++] = input[i];
          break;
      }
    } else {
      out_buffer[out_idx++] = input[i];
    }
  }
  return out_idx;
}

void parse_json_nodes(std::string_view json, std::span<ParseNodeBase*> nodes,
                      std::span<std::string_view> path_stack) {
  std::size_t depth = 0;
  std::string_view current_key;

  std::size_t i = 0;
  auto skip_ws_and_comments = [&]() {
    while (i < json.size()) {
      if (std::isspace(json[i])) {
        i++;
      }
      // Check for the start of a comment
      else if (json[i] == '/' && i + 1 < json.size()) {
        if (json[i + 1] == '/') {
          // Single-line comment: skip until newline
          i += 2;
          while (i < json.size() && json[i] != '\n') i++;
        } else if (json[i + 1] == '*') {
          // Multi-line comment: skip until */
          i += 2;
          while (i + 1 < json.size() && !(json[i] == '*' && json[i + 1] == '/')) {
            i++;
          }
          i += 2;  // skip the closing */
        } else {
          break;  // Just a random slash, break and let the parser handle/fail it
        }
      } else {
        break;  // Found actual JSON content
      }
    }
  };

  while (i < json.size()) {
    skip_ws_and_comments();
    if (i >= json.size()) break;

    char c = json[i];

    if (c == '{') {
      if (!current_key.empty()) {
        // Only store the path if we have room in our span
        if (depth < path_stack.size()) {
          path_stack[depth] = current_key;
        }
        depth++;
        current_key = "";
      }
      i++;
    } else if (c == '}') {
      if (depth > 0) depth--;
      i++;
    } else if (c == ',' || c == ':' || c == '[' || c == ']') {
      // SAFE STRUCTURAL SKIP
      // This natively absorbs trailing commas (e.g., `1, }` reads 1, skips `,`, closes `}`)
      // It also prevents array brackets from being misidentified as primitives.
      i++;
    } else if (c == '"') {
      i++;
      std::size_t start = i;
      while (i < json.size() && json[i] != '"') {
        if (json[i] == '\\') i++;
        i++;
      }
      std::string_view str_val = json.substr(start, i - start);
      if (i < json.size()) i++;

      skip_ws_and_comments();
      if (i < json.size() && json[i] == ':') {
        current_key = str_val;
        i++;
      } else {
        // Only evaluate bindings if we haven't exceeded our tracked path stack
        if (depth < path_stack.size()) {
          DynamicPathView current_path(path_stack, depth, current_key);
          for (auto* n : nodes) {
            if (n->path().matches_parent(current_path)) {
              n->assign(str_val, true, false);
            }
          }
        }
      }
    } else {
      // Must be a primitive value (number, true, false, null)
      std::size_t start = i;

      // Include '[' and ']' in the delimiter list so primitive scanning stops cleanly
      while (i < json.size() && !std::isspace(json[i]) && json[i] != ',' && json[i] != '}' &&
             json[i] != ']' && json[i] != '{' && json[i] != '[') {
        i++;
      }
      std::string_view prim_val = json.substr(start, i - start);

      if (depth < path_stack.size()) {
        bool is_null = (prim_val == "null");
        DynamicPathView current_path(path_stack, depth, current_key);
        for (auto* n : nodes) {
          if (n->path().matches_parent(current_path)) {
            n->assign(prim_val, false, is_null);
          }
        }
      }
    }
  }
}

}  // namespace sjson