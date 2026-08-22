#include "espbase/stack_json/document.hpp"

namespace sjson {

static bool emit_level(Buffer& buffer, std::span<NodeBase*> nodes, const PathBase& current_path) {
  bool is_first_child = true;

  for (auto* n : nodes) {
    if (n->is_emitted() || n->is_omitted()) continue;
    if (!n->path().matches_parent(current_path)) continue;

    if (!is_first_child) {
      if (!buffer.write(",")) return false;
    }

    std::size_t n_depth = n->path().depth();
    std::size_t c_depth = current_path.depth();

    // Write the key
    if (!buffer.write_escaped(n->path().get_element(c_depth))) return false;
    if (!buffer.write(":")) return false;

    if (n_depth == c_depth + 1) {
      // Direct child leaf
      n->emit_value(buffer);
    } else {
      // Intermediate object required
      if (!buffer.write("{")) return false;
      PathView next_path(n->path(), c_depth + 1);

      // Recurse: this inner call will process ALL nodes that share this new path
      if (!emit_level(buffer, nodes, next_path)) return false;

      if (!buffer.write("}")) return false;
    }
    is_first_child = false;
  }
  return true;
}

bool emit_json_nodes(Buffer& buffer, std::span<NodeBase*> nodes) {
  for (auto* n : nodes) n->reset();

  if (!buffer.write("{")) return false;
  if (!nodes.empty()) {
    PathView root_path(nodes[0]->path(), 0);  // Depth 0 represents the root
    if (!emit_level(buffer, nodes, root_path)) return false;
  }
  if (!buffer.write("}")) return false;
  
  return true;
}

}  // namespace sjson