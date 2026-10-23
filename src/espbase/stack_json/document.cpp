#include "espbase/stack_json/document.hpp"

namespace sjson {

static bool emit_level(Buffer& buffer, std::span<NodeBase*> nodes, const PathBase& current_path) {
  bool is_first_child = true;

  for (auto* n : nodes) {
    if (n->is_emitted() || n->is_omitted()) continue;
    PathBase n_path = n->path();
    if (!n_path.matches_parent(current_path)) continue;

    if (!is_first_child) {
      if (!buffer.write(",")) return false;
    }

    std::size_t n_depth = n_path.depth();
    std::size_t c_depth = current_path.depth();

    // Write the key
    if (!buffer.write_escaped(n_path.get_element(c_depth))) return false;
    if (!buffer.write(":")) return false;

    if (n_depth == c_depth + 1) {
      // Direct child leaf
      n->emit_value(buffer);
    } else {
      // Intermediate object required
      if (!buffer.write("{")) return false;
      PathView next_path(n_path, c_depth + 1);

      // Recurse: this inner call will process ALL nodes that share this new path
      if (!emit_level(buffer, nodes, next_path)) return false;

      if (!buffer.write("}")) return false;
    }
    is_first_child = false;
  }
  return true;
}

size_t emit_json_nodes(Buffer& buffer, std::span<NodeBase*> nodes) {
  size_t before = buffer.length();
  for (auto* n : nodes) n->reset();

  if (!buffer.write("{")) return 0;
  if (!nodes.empty()) {
    PathView root_path(nodes[0]->path(), 0);  // Depth 0 represents the root
    if (!emit_level(buffer, nodes, root_path)) return 0;
  }
  if (!buffer.write("}")) return 0;

  return buffer.length() - before;
}

void write_json_value(Buffer& buffer, long long value) {
  Printer::printx(buffer, "%lld", value);
}
void write_json_value(Buffer& buffer, unsigned long long value) {
  Printer::printx(buffer, "%llu", value);
}
void write_json_value(Buffer& buffer, long value) {
  Printer::printx(buffer, "%ld", value);
}
void write_json_value(Buffer& buffer, unsigned long value) {
  Printer::printx(buffer, "%lu", value);
}
void write_json_value(Buffer& buffer, int value) {
  Printer::printx(buffer, "%d", value);
}
void write_json_value(Buffer& buffer, unsigned int value) {
  Printer::printx(buffer, "%u", value);
}
void write_json_value(Buffer& buffer, short value) {
  Printer::printx(buffer, "%d", static_cast<int>(value));
}
void write_json_value(Buffer& buffer, unsigned short value) {
  Printer::printx(buffer, "%u", static_cast<unsigned int>(value));
}
void write_json_value(Buffer& buffer, signed char value) {
  Printer::printx(buffer, "%d", static_cast<int>(value));
}
void write_json_value(Buffer& buffer, unsigned char value) {
  Printer::printx(buffer, "%u", static_cast<unsigned int>(value));
}
void write_json_value(Buffer& buffer, double value) {
  Printer::printx(buffer, "%g", value);
}
void write_json_value(Buffer& buffer, float value) {
  Printer::printx(buffer, "%g", static_cast<double>(value));
}
void write_json_value(Buffer& buffer, bool value) {
  buffer.write(value ? "true" : "false");
}
void write_json_value(Buffer& buffer, std::nullptr_t) {
  buffer.write("null");
}
void write_json_value(Buffer& buffer, const char* value) {
  if (value == nullptr) {
    buffer.write("null");
  } else {
    buffer.write_escaped(value);
  }
}
void write_json_value(Buffer& buffer, std::string_view value) {
  buffer.write_escaped(value);
}
void write_json_value(Buffer& buffer, const std::string& value) {
  buffer.write_escaped(value);
}

}  // namespace sjson