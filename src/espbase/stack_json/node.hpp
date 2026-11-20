#pragma once

#include "espbase/stack_json/detail/node_impl.hpp"

namespace sjson {

template <typename PathT, typename ValueT>
auto node(PathT&& p, ValueT&& val) {
  using ValType = node_value_t<ValueT>;
  return ValueNode<std::decay_t<PathT>, ValType>(std::forward<PathT>(p),
                                                 static_cast<ValType>(std::forward<ValueT>(val)));
}

template <typename ValueT>
auto node(const char* root_key, ValueT&& val) {
  return node(path(root_key), std::forward<ValueT>(val));
}

template <typename PathT, typename ValueT>
auto node_if(bool condition, PathT&& p, ValueT&& val) {
  auto n = node(std::forward<PathT>(p), std::forward<ValueT>(val));
  n.set_omitted(!condition);
  return n;
}

template <typename ValueT>
auto node_if(bool condition, const char* root_key, ValueT&& val) {
  return node_if(condition, path(root_key), std::forward<ValueT>(val));
}

template <typename PathT>
auto node_if(PathT&& p, const char* val) {
  // std::move forces ValueT to deduce as 'const char*' instead of 'const char*&'
  // This guarantees the node copies the pointer itself, rather than referencing the local variable.
  return node_if(val != nullptr, std::forward<PathT>(p), std::move(val));
}

inline auto node_if(const char* root_key, const char* val) {
  return node_if(val != nullptr, path(root_key), std::move(val));
}

}  // namespace sjson
