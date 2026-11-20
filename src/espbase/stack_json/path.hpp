#pragma once

#include <string_view>
#include <type_traits>
#include <utility>

namespace sjson {

class PathBase {
 protected:
  const std::string_view* data_ = nullptr;
  std::size_t size_ = 0;

 public:
  constexpr PathBase() = default;
  constexpr PathBase(const std::string_view* data, std::size_t size) : data_(data), size_(size) {}

  constexpr std::size_t depth() const { return size_; }
  constexpr std::string_view get_element(std::size_t index) const { return data_[index]; }
  constexpr const std::string_view* data() const { return data_; }

  bool matches_parent(const PathBase& parent) const {
    if (size_ < parent.size_) return false;
    for (std::size_t i = 0; i < parent.size_; ++i) {
      if (data_[i] != parent.data_[i]) return false;
    }
    return true;
  }
};

}  // namespace sjson

// Pull in StaticPath, PathView, and sjson::detail::safe_string_view
#include "espbase/stack_json/detail/path_impl.hpp"

namespace sjson {

template <typename... Args>
auto path(Args&&... args) {
  return StaticPath<sizeof...(Args)>(std::forward<Args>(args)...);
}

}  // namespace sjson
