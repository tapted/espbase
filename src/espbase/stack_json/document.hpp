#pragma once

#include <span>
#include <tuple>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/node.hpp"

namespace sjson {

bool emit_json_nodes(Buffer& buffer, std::span<NodeBase*> nodes);

template <typename... Nodes>
class Document {
  std::tuple<Nodes...> nodes_;

 public:
  explicit Document(Nodes... nodes) : nodes_(std::move(nodes)...) {}

  bool emit(Buffer& buffer) {
    // Generate an array of base pointers on the stack
    std::array<NodeBase*, sizeof...(Nodes)> node_ptrs =
        []<std::size_t... I>(std::tuple<Nodes...>& t, std::index_sequence<I...>) {
          return std::array<NodeBase*, sizeof...(Nodes)>{&std::get<I>(t)...};
        }(nodes_, std::make_index_sequence<sizeof...(Nodes)>{});

    // Pass to the non-templated traversal function
    return emit_json_nodes(buffer, node_ptrs);
  }
};

template <typename... Nodes>
auto stack_json(Nodes... nodes) {
  return Document<Nodes...>(std::move(nodes)...);
}

}  // namespace sjson