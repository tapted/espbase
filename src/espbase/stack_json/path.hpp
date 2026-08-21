#pragma once

#include <array>
#include <string_view>

namespace sjson {

class PathBase {
 public:
  virtual ~PathBase() = default;
  virtual std::size_t depth() const = 0;
  virtual std::string_view get_element(std::size_t index) const = 0;

  // Checks if this path matches an open parent path up to the parent's depth
  bool matches_parent(const PathBase& parent) const {
    if (depth() <= parent.depth()) return false;
    for (std::size_t i = 0; i < parent.depth(); ++i) {
      if (get_element(i) != parent.get_element(i)) return false;
    }
    return true;
  }
};

// A concrete path holding a fixed number of string_views (great for RO data)
template <std::size_t Depth>
class StaticPath : public PathBase {
  std::array<std::string_view, Depth> elements_;

 public:
  template <typename... Args>
  constexpr StaticPath(Args... args) : elements_{std::string_view(args)...} {
    static_assert(sizeof...(Args) == Depth, "Depth mismatch");
  }

  std::size_t depth() const override { return Depth; }
  std::string_view get_element(std::size_t index) const override { return elements_[index]; }
};

// Builder for Paths
template <typename... Args>
auto path(Args... args) {
  return StaticPath<sizeof...(Args)>(args...);
}

}