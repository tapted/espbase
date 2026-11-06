#include "espbase/stack_json/document.hpp"

#include <cstring>
#include <type_traits>

#include "espbase/stack_json/array_emitter.hpp"

namespace sjson {

static bool emit_level(Buffer& buffer, std::span<NodeBase*> nodes, const PathBase& current_path) {
  bool is_first_child = true;
  std::size_t c_depth = current_path.depth();

  for (auto* n : nodes) {
    if (n->is_emitted() || n->is_omitted()) continue;
    PathBase n_path = n->path();
    if (c_depth > 0 && !n_path.matches_parent(current_path)) continue;

    if (!is_first_child) {
      if (!buffer.write(",")) return false;
    }

    std::size_t n_depth = n_path.depth();

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
  if (nodes.empty()) {
    return buffer.write("{}");
  }

  // Fast-path: Check if all nodes are depth-1 leaves (the vast majority of objects)
  bool has_nested = false;
  for (auto* n : nodes) {
    if (n->path().depth() > 1) {
      has_nested = true;
      break;
    }
  }

  if (!has_nested) {
    if (!buffer.write("{")) return 0;
    bool is_first = true;
    for (auto* n : nodes) {
      if (n->is_omitted()) continue;
      if (!is_first) {
        if (!buffer.write(",")) return 0;
      }
      PathBase p = n->path();
      if (!buffer.write_escaped(p.get_element(0))) return 0;
      if (!buffer.write(":")) return 0;
      n->emit_value(buffer);
      is_first = false;
    }
    if (!buffer.write("}")) return 0;
    return buffer.length() - before;
  }

  // Recursive hierarchical traversal for nested schemas
  for (auto* n : nodes) n->reset();
  if (!buffer.write("{")) return 0;
  PathView root_path(nodes[0]->path(), 0);  // Depth 0 represents the root
  if (!emit_level(buffer, nodes, root_path)) return 0;
  if (!buffer.write("}")) return 0;

  return buffer.length() - before;
}

template <typename UIntT>
static inline void write_unsigned(Buffer& buffer, UIntT value) {
  if (buffer.bad()) return;
  std::span<char> span = buffer.get_write_span();
  if (span.empty()) {
    buffer.fail();
    return;
  }
  char temp[24];
  char* p = temp + sizeof(temp);
  do {
    *--p = static_cast<char>('0' + (value % 10));
    value /= 10;
  } while (value > 0);

  std::size_t len = (temp + sizeof(temp)) - p;
  if (len > span.size()) {
    buffer.fail();
    return;
  }
  std::memcpy(span.data(), p, len);
  buffer.commit(len);
}

template <typename IntT>
static inline void write_signed(Buffer& buffer, IntT value) {
  if (buffer.bad()) return;
  std::span<char> span = buffer.get_write_span();
  if (span.empty()) {
    buffer.fail();
    return;
  }
  using UIntT = std::make_unsigned_t<IntT>;
  char temp[24];
  char* p = temp + sizeof(temp);
  bool neg = value < 0;
  UIntT uval = neg ? 0 - static_cast<UIntT>(value) : static_cast<UIntT>(value);
  do {
    *--p = static_cast<char>('0' + (uval % 10));
    uval /= 10;
  } while (uval > 0);

  if (neg) {
    *--p = '-';
  }

  std::size_t len = (temp + sizeof(temp)) - p;
  if (len > span.size()) {
    buffer.fail();
    return;
  }
  std::memcpy(span.data(), p, len);
  buffer.commit(len);
}

void write_json_value(Buffer& buffer, long long value) {
  write_signed(buffer, value);
}
void write_json_value(Buffer& buffer, unsigned long long value) {
  write_unsigned(buffer, value);
}
void write_json_value(Buffer& buffer, long value) {
  write_signed(buffer, value);
}
void write_json_value(Buffer& buffer, unsigned long value) {
  write_unsigned(buffer, value);
}
void write_json_value(Buffer& buffer, int value) {
  write_signed(buffer, value);
}
void write_json_value(Buffer& buffer, unsigned int value) {
  write_unsigned(buffer, value);
}
void write_json_value(Buffer& buffer, short value) {
  write_signed(buffer, value);
}
void write_json_value(Buffer& buffer, unsigned short value) {
  write_unsigned(buffer, value);
}
void write_json_value(Buffer& buffer, signed char value) {
  write_signed(buffer, static_cast<int>(value));
}
void write_json_value(Buffer& buffer, unsigned char value) {
  write_unsigned(buffer, static_cast<unsigned int>(value));
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
