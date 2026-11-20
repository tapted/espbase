#pragma once

#include "espbase/stack_json/detail/document_impl.hpp"

namespace sjson {

// Generic Container Overload (Catches std::vector, std::array, C-arrays)
// lvalues only: StackJson doesn't call copy constructors!
template <typename Container>
auto span_array(Container& c) {
  // Deduce the element type by looking at what std::data() returns
  using ElementT = std::remove_pointer_t<decltype(std::data(c))>;

  // Construct the span directly and pass it to the document
  return SpanArrayDocument<ElementT>(std::span<ElementT>(std::data(c), std::size(c)));
}

// Explicit Span Overload (For when you are passing a temporary std::span view)
template <typename ElementT>
auto span_array(std::span<ElementT> s) {
  return SpanArrayDocument<ElementT>(s);
}

template <typename... Elements>
auto stack_array(Elements&&... elems) {
  return ArrayDocument<Elements...>(std::forward<Elements>(elems)...);
}

template <typename Callback>
auto stream_array(Callback&& cb) {
  return StreamArrayDocument<std::decay_t<Callback>>(std::forward<Callback>(cb));
}

template <typename... Nodes>
auto stack_json(Nodes&&... nodes) {
  static_assert((std::is_base_of_v<NodeBase, std::decay_t<Nodes>> && ...),
                "StackJson Error: All arguments to stack_json() must be Nodes. "
                "Ensure keys and values are using node() or node_if().");
  return Document<Nodes...>(std::forward<Nodes>(nodes)...);
}

}  // namespace sjson
