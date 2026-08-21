#include "espbase/stack_json/document.hpp"

namespace sjson {

static void emit_level(Buffer& buffer, std::span<NodeBase*> nodes, const PathBase& current_path) {
  bool is_first_child = true;

  for (auto* n : nodes) {
    if (n->is_emitted()) continue;
    if (!n->path().matches_parent(current_path)) continue;

    if (!is_first_child) {
      buffer.write(",");
    }

    std::size_t n_depth = n->path().depth();
    std::size_t c_depth = current_path.depth();

    // Write the key
    buffer.write_escaped(n->path().get_element(c_depth));
    buffer.write(":");

    if (n_depth == c_depth + 1) {
      // Direct child leaf
      n->emit_value(buffer);
    } else {
      // Intermediate object required
      buffer.write("{");
      PathView next_path(n->path(), c_depth + 1);

      // Recurse: this inner call will process ALL nodes that share this new path
      emit_level(buffer, nodes, next_path);

      buffer.write("}");
    }
    is_first_child = false;
  }
}

bool emit_json_nodes(Buffer& buffer, std::span<NodeBase*> nodes) {
  for (auto* n : nodes) n->reset();

  buffer.write("{");
  if (!nodes.empty()) {
    PathView root_path(nodes[0]->path(), 0);  // Depth 0 represents the root
    emit_level(buffer, nodes, root_path);
  }
  buffer.write("}");

  return true;
}

}  // namespace sjson