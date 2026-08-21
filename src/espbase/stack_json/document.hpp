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

  // Extracts the pointers from the tuple into a flat array (to merge with another document)
  void append_pointers_to(std::span<NodeBase*> dest, std::size_t& count) {
    auto append = [&]<std::size_t... I>(std::index_sequence<I...>) {
      // Fold expression with bounds checking
      ((count < dest.size() ? dest[count++] = &std::get<I>(nodes_) : nullptr), ...);
    };
    append(std::make_index_sequence<sizeof...(Nodes)>{});
  }

  void emit_value(Buffer& buffer) {
    if constexpr (sizeof...(Nodes) == 0) {
      buffer.write("{}");
    } else {
      std::array<NodeBase*, sizeof...(Nodes)> node_ptrs =
          [&]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<NodeBase*, sizeof...(Nodes)>{&std::get<I>(nodes_)...};
          }(std::make_index_sequence<sizeof...(Nodes)>{});

      emit_json_nodes(buffer, node_ptrs);  // The compiled tree traversal function
    }
  }

  bool emit(Buffer& buffer) {
    emit_value(buffer);
    return true;
  }
};

template <typename... Elements>
class ArrayDocument {
  std::tuple<Elements...> elements_;

 public:
  explicit ArrayDocument(Elements... elems) : elements_(std::move(elems)...) {}

  void emit_value(Buffer& buffer) {
    buffer.write("[");
    bool first = true;

    // C++20 lambda generic iteration
    auto emit_elem = [&](auto& elem) {
      if (!first) buffer.write(",");
      write_json_value(buffer, elem);
      first = false;
    };

    // Unpack tuple and apply to all elements
    std::apply([&](auto&... args) { (emit_elem(args), ...); }, elements_);

    buffer.write("]");
  }
};

template <typename... Elements>
auto stack_array(Elements... elems) {
  return ArrayDocument<Elements...>(std::move(elems)...);
}

template <typename... Nodes>
auto stack_json(Nodes... nodes) {
  return Document<Nodes...>(std::move(nodes)...);
}

}  // namespace sjson