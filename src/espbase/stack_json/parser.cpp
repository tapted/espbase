#include "espbase/stack_json/parser.hpp"

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
  auto skip_ws = [&]() {
    while (i < json.size() && std::isspace(json[i])) i++;
  };

  while (i < json.size()) {
    skip_ws();
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
    } else if (c == ',') {
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

      skip_ws();
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
      std::size_t start = i;
      while (i < json.size() && !std::isspace(json[i]) && json[i] != ',' && json[i] != '}' &&
             json[i] != ']')
        i++;
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