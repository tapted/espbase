#pragma once

#include <tuple>

#include "espbase/stack_json/buffer.hpp"
#include "espbase/stack_json/node.hpp"

namespace sjson {

template <typename... Nodes>
class Document {
  std::tuple<Nodes...> nodes_;

 public:
  explicit Document(Nodes... nodes) : nodes_(std::move(nodes)...) {}

  bool emit(Buffer& buffer) {
    // Create an array of base pointers pointing to the concrete types in the tuple
    std::array<NodeBase*, sizeof...(Nodes)> node_ptrs =
        []<std::size_t... I>(std::tuple<Nodes...>& t, std::index_sequence<I...>) {
          return std::array<NodeBase*, sizeof...(Nodes)>{&std::get<I>(t)...};
        }(nodes_, std::make_index_sequence<sizeof...(Nodes)>{});

    // 1. Reset all flags
    for (auto* n : node_ptrs) n->reset();

    // 2. Multi-pass algorithm (simplified outline)
    // You would start with a RootPath (depth 0) and iterate,
    // recursing when you hit HAS_CHILDREN.

    return true;
  }
};

template <typename... Nodes>
auto stack_json(Nodes... nodes) {
  return Document<Nodes...>(std::move(nodes)...);
}

}  // namespace sjson